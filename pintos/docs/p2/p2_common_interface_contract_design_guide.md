# Pintos P2 공통 인터페이스 계약 설계 가이드

## 0. 이 문서의 목적

이 문서는 `p2_user_memory_syscall_parallel_work_guide.md`의 `3. 공통 인터페이스 계약`을 실제로 구현하기 전에 팀이 무엇을 합의해야 하고, 어떤 방향으로 코드를 나누어 만들어야 하는지 설명한다.

공통 인터페이스 계약은 단순히 헤더 파일 몇 개를 만드는 일이 아니다. 세 명이 병렬로 구현할 때 서로의 내부 구현을 몰라도 같은 이름, 같은 실패 정책, 같은 자원 정리 규칙을 믿고 작업할 수 있게 만드는 약속이다.

이 문서에서 다루는 범위는 다음과 같다.

- `uaccess` 사용자 메모리 접근 계층을 왜 만들고 어떻게 설계할지
- `fd` 파일 디스크립터 계층을 왜 만들고 어떻게 설계할지
- `struct thread`에 어떤 필드를 어떤 책임 경계로 추가할지
- syscall 실패 경로, `filesys_lock`, wait/exit/fork가 공유할 lifecycle 자료구조를 어떻게 합의할지
- 팀원이 각자 구현을 시작하기 전 어떤 순서로 토대를 깔면 좋은지

이 문서는 구현 완료 문서가 아니라 구현 전 설계 가이드다. 따라서 여기의 코드는 대부분 인터페이스, 의사코드, 체크리스트로 봐야 한다.

## 1. 공통 인터페이스 계약이 필요한 이유

Pintos Project 2의 syscall 구현은 겉으로 보면 syscall별로 나눌 수 있어 보인다. 하지만 실제로는 거의 모든 syscall이 다음 공통 문제를 공유한다.

- user program이 넘긴 pointer가 안전한지 검사해야 한다.
- 파일 syscall은 process별 fd table을 사용해야 한다.
- fork는 address space, fd table, parent-child 상태를 함께 복제해야 한다.
- exit와 wait는 같은 child status record를 통해 종료 상태를 전달해야 한다.
- exec와 파일 syscall은 같은 파일 시스템 락 규칙을 따라야 한다.

공통 인터페이스 없이 각자 구현하면 이런 문제가 생긴다.

- 어떤 사람은 invalid pointer에서 `thread_exit()`을 직접 호출하고, 다른 사람은 `exit(-1)`을 호출한다.
- 어떤 사람은 fd table을 배열로 만들고, 다른 사람은 list 기반으로 가정한다.
- fork 담당자는 fd 복제 함수 이름을 모르고 직접 table 내부를 만진다.
- exit 담당자는 fd 정리 함수가 없어 파일 자원을 빠뜨린다.
- wait 담당자는 child 상태가 thread 내부에 있다고 가정하지만, child thread가 이미 사라진 뒤 parent가 dangling pointer를 본다.

그래서 먼저 해야 할 일은 “각 모듈이 제공할 함수 이름과 실패 정책”을 고정하는 것이다. 내부 구현은 각 담당자가 바꿀 수 있지만, 외부에서 호출하는 계약은 흔들리면 안 된다.

## 2. 최종 목표 그림

최종적으로 코드는 다음 구조에 가까워야 한다.

```text
syscall.c
  - syscall 번호 dispatch
  - register에서 인자 추출
  - uaccess/fd/process helper 호출
  - 반환값을 rax에 저장

uaccess.c / uaccess.h
  - user pointer 검증
  - user memory <-> kernel memory 복사
  - invalid pointer면 현재 user process를 exit(-1)

fd.c / fd.h
  - process별 fd table 관리
  - fd 할당, 조회, 닫기, 전체 닫기
  - fork용 fd table 복제

process.c / process.h
  - fork, exec, wait, exit lifecycle
  - child status record 관리
  - executable deny-write 관리

thread.h
  - 각 process가 들고 있어야 하는 최소 상태 필드
```

중요한 방향은 `syscall.c`를 거대한 파일로 만들지 않는 것이다. `syscall.c`는 교통정리 담당이고, pointer 안전성은 `uaccess`, fd 자원은 `fd`, process lifecycle은 `process.c`로 보내는 편이 분업 충돌이 적다.

## 3. 먼저 합의해야 하는 결정 목록

구현 전에 팀이 한 번에 합의해야 하는 항목은 다음과 같다.

| 항목 | 추천 결정 | 이유 |
| --- | --- | --- |
| invalid pointer 처리 | 현재 process를 `exit(-1)` | 테스트가 kernel panic이 아니라 user process 종료를 기대한다. |
| `size == 0` buffer | 성공 처리 | `read-zero`, `write-zero`에서 pointer를 건드리지 않아야 한다. |
| 문자열 복사 | kernel page 또는 malloc buffer에 복사 | 파일명과 cmdline은 filesystem/process 함수에 넘기기 전에 kernel 소유가 되어야 한다. |
| fd table 크기 | `FD_MAX 128` | Project 2 기본 테스트에 충분하고 배열 구현이 단순하다. |
| fd 0, 1 | stdin/stdout 예약 | `open()`은 절대 0, 1을 반환하지 않는다. |
| invalid fd | `read/write/filesize/tell`은 `-1`, `close`는 no-op | invalid fd 테스트에서 panic을 피한다. |
| 파일 시스템 락 | 전역 `filesys_lock` 하나 | `filesys_*`, `file_*` 접근 규칙을 통일한다. |
| user pointer 검증과 락 순서 | pointer 검증 먼저, 락 나중 | invalid pointer 종료 시 lock leak을 막는다. |
| child 상태 저장 | 별도 동적 `child_status` record | child thread 종료 후에도 parent가 wait할 수 있어야 한다. |
| exit message 위치 | 한 곳에서만 출력 | 중복 출력은 expected output을 깨뜨린다. |

이 표를 팀에서 먼저 확정한 뒤 헤더 파일을 만든다. 헤더 파일이 만들어진 뒤에는 각 담당자가 내부 구현을 병렬로 진행한다.

## 4. 사용자 메모리 API 설계

### 4.1 제공할 인터페이스

`include/userprog/uaccess.h`에는 다음 함수 이름을 고정한다.

```c
void validate_user_read (const void *uaddr, size_t size);
void validate_user_write (void *uaddr, size_t size);

void copy_in (void *dst, const void *usrc, size_t size);
void copy_out (void *udst, const void *src, size_t size);

char *copy_in_string (const char *ustr);
```

이 API의 의미는 다음과 같다.

- `validate_user_read()`는 커널이 user buffer를 읽어도 되는지 확인한다.
- `validate_user_write()`는 커널이 user buffer에 써도 되는지 확인한다.
- `copy_in()`은 user memory에서 kernel memory로 bytes를 복사한다.
- `copy_out()`은 kernel memory에서 user memory로 bytes를 복사한다.
- `copy_in_string()`은 user string을 kernel buffer로 복사해서 반환한다.

2번/3번 담당자는 user pointer를 직접 검사하지 말고 이 함수만 호출한다.

### 4.2 read/write 방향을 헷갈리지 않는 법

syscall 이름만 보고 readable/writable을 정하면 실수하기 쉽다. 기준은 “커널이 user memory에 대해 어떤 행동을 하는가”다.

| syscall | user buffer | 커널 입장 | 사용할 검증 |
| --- | --- | --- | --- |
| `write(fd, buffer, size)` | `buffer` | user buffer를 읽는다 | `validate_user_read()` |
| `read(fd, buffer, size)` | `buffer` | user buffer에 쓴다 | `validate_user_write()` |
| `create(file, size)` | `file` | user string을 읽는다 | `copy_in_string()` |
| `open(file)` | `file` | user string을 읽는다 | `copy_in_string()` |
| `exec(cmd_line)` | `cmd_line` | user string을 읽는다 | `copy_in_string()` |
| `fork(thread_name)` | `thread_name` | user string을 읽는다 | `copy_in_string()` |

특히 `read()`의 buffer는 writable 검사를 해야 한다. 이 부분을 놓치면 read-only user page에 커널이 쓰다가 kernel page fault가 날 수 있다.

### 4.3 validate 구현 방향

`validate_user_read()`와 `validate_user_write()`는 같은 내부 helper를 공유하는 편이 좋다.

추천 구조:

```c
static void
validate_user_buffer (const void *uaddr, size_t size, bool writable)
{
    // 1. size == 0이면 즉시 성공
    // 2. NULL 검사
    // 3. start + size - 1 overflow 검사
    // 4. start/end가 user virtual address인지 검사
    // 5. start page부터 end page까지 모든 page 검사
    // 6. writable이면 PTE_W까지 검사
}
```

검사해야 하는 항목은 다음과 같다.

- `uaddr == NULL`
- `start + size - 1` 계산 overflow
- 시작 주소 또는 끝 주소가 kernel address
- 중간 page 중 unmapped page 존재
- writable 검사가 필요한데 PTE에 write bit가 없음

시작 주소와 마지막 주소만 검사하면 부족하다. 예를 들어 buffer가 세 page를 걸치고 가운데 page만 unmapped이면 시작/끝 검사는 통과하지만 실제 복사 중 page fault가 난다.

### 4.4 PTE 확인 방식

현재 Project 2에서는 page table에 이미 올라온 user page를 검사하면 된다. 일반적인 방향은 다음과 같다.

```c
uint64_t *pte = pml4e_walk (thread_current ()->pml4, (uint64_t) page, false);

if (pte == NULL || (*pte & PTE_P) == 0 || (*pte & PTE_U) == 0)
    exit(-1);

if (writable && (*pte & PTE_W) == 0)
    exit(-1);
```

주의할 점은 `pml4_get_page()`만으로 writable 여부를 알 수 없다는 것이다. writable 검사를 하려면 PTE flag를 봐야 한다.

### 4.5 copy_in/copy_out 구현 방향

가장 단순한 구현은 검증 후 `memcpy()`다.

```c
void
copy_in (void *dst, const void *usrc, size_t size)
{
    validate_user_read (usrc, size);
    memcpy (dst, usrc, size);
}

void
copy_out (void *udst, const void *src, size_t size)
{
    validate_user_write (udst, size);
    memcpy (udst, src, size);
}
```

검증을 먼저 끝냈기 때문에 `memcpy()` 도중 kernel panic이 날 가능성을 줄인다. 이후 Project 3에서 lazy loading이나 stack growth와 결합할 때는 page fault 처리 정책을 다시 볼 수 있지만, Project 2의 공통 계약은 “미리 검증하고 복사한다”로 고정하면 된다.

### 4.6 copy_in_string 구현 방향

`copy_in_string()`은 파일명과 command line 처리의 공통 진입점이다.

추천 정책:

- user string을 한 byte씩 읽으며 null terminator를 찾는다.
- 각 byte 접근 전 해당 주소가 readable인지 검사한다.
- 최대 길이를 정한다. Project 2에서는 page 하나(`PGSIZE`)를 상한으로 잡으면 구현이 단순하다.
- null terminator를 찾으면 kernel buffer를 할당하고 문자열 전체를 복사한다.
- 호출자는 사용 후 반드시 해제한다.

의사코드:

```c
char *
copy_in_string (const char *ustr)
{
    // 1. ustr부터 한 byte씩 validate_user_read(addr, 1)
    // 2. '\0'을 찾을 때까지 길이 계산
    // 3. 너무 길면 exit(-1)
    // 4. kernel buffer 할당
    // 5. user string을 kernel buffer로 복사
    // 6. kernel buffer 반환
}
```

이 함수가 필요한 이유는 filesystem이나 process loading 코드가 user pointer를 직접 들고 있으면 위험하기 때문이다. syscall 진입 직후에 kernel-owned string으로 바꾸면 이후 로직은 user memory 문제에서 분리된다.

### 4.7 실패 정책

`uaccess` 계층은 실패를 bool로 반환하지 않고 현재 process를 종료하는 정책을 추천한다.

이유는 다음과 같다.

- invalid pointer는 syscall별 실패값을 반환하는 상황이 아니라 프로세스 비정상 종료 상황이다.
- 모든 syscall에서 `if (!valid) exit(-1)`를 반복하지 않아도 된다.
- 팀 전체가 같은 실패 정책을 공유한다.

다만 exit message, child status 저장, fd 정리는 process lifecycle 담당 구현과 연결되어야 한다. 따라서 최종적으로는 `uaccess`가 직접 `thread_exit()`을 부르는 것보다 공용 `syscall_exit(-1)` 또는 동일한 의미의 helper를 호출하도록 맞추는 편이 좋다.

## 5. FD API 설계

### 5.1 제공할 인터페이스

`include/userprog/fd.h`에는 다음 계약을 둔다.

```c
#define FD_MAX 128

void fd_init (struct thread *t);
int fd_alloc (struct file *file);
struct file *fd_get (int fd);
void fd_close (int fd);
void fd_close_all (void);
bool fd_duplicate_all (struct thread *dst, struct thread *src);
```

이 API의 의미는 다음과 같다.

- `fd_init()`은 새 process의 fd table을 초기화한다.
- `fd_alloc()`은 `struct file *`를 process fd table에 넣고 fd 번호를 반환한다.
- `fd_get()`은 fd 번호로 file 객체를 조회한다.
- `fd_close()`는 fd 하나를 닫는다.
- `fd_close_all()`은 현재 process의 열린 fd를 모두 닫는다.
- `fd_duplicate_all()`은 fork 시 parent fd table을 child에 복제한다.

파일 syscall 담당자는 이 API 내부 구현을 소유한다. fork/exit 담당자는 fd table 배열 내부를 직접 만지지 않고 이 API만 사용한다.

### 5.2 thread 필드 설계

FD 기능을 위해 `struct thread`에 다음 필드를 둔다.

```c
#ifdef USERPROG
    struct file *fd_table[FD_MAX];
    int next_fd;
    struct file *running_file;
#endif
```

각 필드의 의미:

- `fd_table`: 현재 process가 연 파일 객체 배열
- `next_fd`: 다음 fd 할당을 시작할 위치
- `running_file`: 현재 실행 중인 executable file

`running_file`은 rox 테스트를 위해 필요하다. executable에 `file_deny_write()`를 걸고 process가 살아 있는 동안 파일을 닫지 않아야 write deny가 유지된다.

### 5.3 fd_init 구현 방향

`fd_init()`은 process 시작 또는 fork child 초기화 시 호출된다.

```c
void
fd_init (struct thread *t)
{
    // fd_table 전체를 NULL로 초기화
    // next_fd = 2
    // running_file = NULL
}
```

fd `0`과 `1`은 stdin/stdout 예약이므로 일반 파일을 넣지 않는다.

### 5.4 fd_alloc 구현 방향

`fd_alloc()`은 `file`을 fd table의 빈 slot에 넣고 fd 번호를 반환한다.

정책:

- `file == NULL`이면 `-1`
- 검색은 fd `2`부터 시작
- `next_fd`를 hint로 사용하면 close 후 재사용이 자연스럽다.
- table이 가득 차면 `-1`
- 실패 시 `file_close(file)`을 자동으로 할지 말지 팀에서 정한다.

추천은 `fd_alloc()`이 실패한 파일을 닫지 않는 것이다. 이유는 소유권이 명확하기 때문이다. `open()` 구현자는 다음처럼 처리한다.

```c
struct file *file = filesys_open (name);
int fd = fd_alloc (file);
if (fd < 0)
    file_close (file);
return fd;
```

이 정책을 문서화하지 않으면 어떤 사람은 `fd_alloc()`이 닫아준다고 생각하고, 다른 사람은 caller가 닫는다고 생각해 double close나 leak이 생길 수 있다.

### 5.5 fd_get 구현 방향

`fd_get()`은 invalid fd에 대해 `NULL`을 반환한다.

```c
if (fd < 2 || fd >= FD_MAX)
    return NULL;
return thread_current ()->fd_table[fd];
```

파일 syscall에서는 다음처럼 사용한다.

```c
struct file *file = fd_get (fd);
if (file == NULL)
    return -1;
```

`read(fd=0)`과 `write(fd=1)`은 special case이므로 `fd_get()` 전에 syscall별로 처리한다.

### 5.6 fd_close / fd_close_all 구현 방향

`fd_close(fd)` 정책:

- fd가 invalid이면 아무 일도 하지 않는다.
- fd slot이 비어 있으면 아무 일도 하지 않는다.
- valid file이면 `file_close()` 후 slot을 `NULL`로 만든다.

`fd_close_all()` 정책:

- fd `2`부터 끝까지 순회하며 열린 파일을 모두 닫는다.
- process exit에서 반드시 호출한다.
- stdin/stdout 예약 slot은 닫지 않는다.

`close-bad-fd`, `close-twice`는 panic 없이 지나가야 한다. 따라서 `ASSERT(file != NULL)` 같은 접근은 피해야 한다.

### 5.7 fd_duplicate_all 구현 방향

fork에서 parent와 child가 같은 `struct file *`를 공유하면 안 된다. `file_duplicate()`로 독립 file object를 만들어야 한다.

```c
bool
fd_duplicate_all (struct thread *dst, struct thread *src)
{
    // dst fd table 초기화
    // src의 fd 2..FD_MAX-1 순회
    // 각 file을 file_duplicate()로 복제
    // 실패하면 지금까지 복제한 dst fd를 모두 닫고 false
    // running_file도 필요하면 file_duplicate()
    // next_fd 복사
}
```

중요한 점:

- 실패 시 부분 복제된 파일을 정리해야 한다.
- parent/child가 같은 file object를 공유하지 않아야 한다.
- file offset은 `file_duplicate()`가 복사한다.
- `running_file` 복제 정책은 rox 구현 방식과 함께 맞춘다.

## 6. 파일 시스템 락 설계

Pintos의 filesys/file layer는 여러 thread가 동시에 들어와도 안전하다고 가정하면 안 된다. 따라서 Project 2에서는 전역 락 하나로 보호하는 방식이 단순하고 충분하다.

추천 계약:

```c
extern struct lock filesys_lock;
```

초기화 위치:

- `syscall_init()` 또는 userprog 초기화 경로에서 `lock_init(&filesys_lock)` 호출

락을 잡아야 하는 함수:

- `filesys_create()`
- `filesys_remove()`
- `filesys_open()`
- `file_read()`
- `file_write()`
- `file_length()`
- `file_seek()`
- `file_tell()`
- `file_close()`
- `file_duplicate()`
- `file_deny_write()`
- `file_allow_write()`

락을 잡으면 안 되는 구간:

- user pointer 검증
- `copy_in_string()`
- `copy_in()`
- `copy_out()`

순서는 항상 다음처럼 잡는다.

```text
1. syscall 인자 추출
2. user pointer 검증 및 kernel buffer 복사
3. filesys_lock acquire
4. filesys/file 함수 호출
5. filesys_lock release
6. kernel buffer 해제
7. 반환값 설정
```

이 순서를 지키면 invalid pointer가 들어왔을 때 lock을 잡은 채 process가 종료되는 일을 피할 수 있다.

## 7. syscall dispatcher와 공용 exit 경로

### 7.1 dispatcher의 역할

`syscall.c`는 다음 일만 담당하는 것을 추천한다.

- `f->R.rax`에서 syscall 번호 읽기
- register에서 인자 추출
- 필요한 `uaccess`, `fd`, `process` helper 호출
- 반환값을 `f->R.rax`에 저장
- 알 수 없는 syscall이면 `exit(-1)`

syscall 인자 register는 다음과 같다.

| 순서 | register |
| --- | --- |
| syscall 번호 | `rax` |
| 1번 인자 | `rdi` |
| 2번 인자 | `rsi` |
| 3번 인자 | `rdx` |
| 4번 인자 | `r10` |
| 5번 인자 | `r8` |
| 6번 인자 | `r9` |
| 반환값 | `rax` |

### 7.2 공용 exit helper

invalid pointer, 명시적 `exit(status)`, load failure, exception 종료가 모두 exit 상태 저장과 자원 정리를 필요로 한다. 그래서 공용 exit helper를 하나 두는 것을 추천한다.

예시 계약:

```c
void syscall_exit (int status) NO_RETURN;
```

이 helper 또는 그 아래 process lifecycle 함수가 해야 할 일:

- exit status 저장
- exit message 출력
- fd 전체 정리
- `running_file` 닫기
- child status record에 종료 상태 기록
- parent가 wait 중이면 깨우기
- `thread_exit()` 호출

주의할 점은 exit message를 한 곳에서만 출력하는 것이다. `syscall_exit()`과 `process_exit()` 양쪽에서 출력하면 테스트 output이 깨진다.

## 8. process lifecycle 필드와 child status record 설계

공통 계약 문서에는 FD 필드만 구체적으로 들어 있지만, fork/wait/exit도 시작 전에 구조를 합의해야 한다.

### 8.1 왜 별도 child_status record가 필요한가

parent는 child가 종료된 뒤에도 `wait(child_tid)`로 exit status를 회수할 수 있어야 한다. 하지만 child의 `struct thread`는 child 종료 후 스케줄러에 의해 해제될 수 있다.

따라서 다음 방식은 위험하다.

```c
// 위험한 방향
parent가 child struct thread *를 저장
wait에서 child->exit_status를 직접 읽음
```

child thread가 이미 해제되면 parent는 dangling pointer를 읽게 된다.

추천 방향은 parent와 child가 함께 참조하는 동적 record를 따로 두는 것이다.

```c
struct child_status {
    tid_t tid;
    int exit_status;
    bool exited;
    bool waited;
    struct semaphore wait_sema;
    struct list_elem elem;
    int ref_cnt;
};
```

필드 이름은 팀이 바꿔도 되지만, 개념은 유지해야 한다.

### 8.2 struct thread에 필요한 lifecycle 필드

추천 개념:

```c
#ifdef USERPROG
    struct list children;
    struct child_status *child_status;
    int exit_status;
#endif
```

각 필드의 의미:

- `children`: 현재 process가 직접 만든 child들의 status record 목록
- `child_status`: 현재 process가 parent에게 보고할 내 status record
- `exit_status`: 현재 process의 종료 상태

parent는 자신의 `children` list에서 wait 대상 record를 찾는다. child는 종료할 때 자신의 `child_status` record에 exit status를 기록하고 semaphore를 올린다.

### 8.3 reference count 또는 orphan 처리

child status record는 parent와 child 두 주체가 공유한다.

가능한 수명 흐름:

- parent가 먼저 wait하고 child가 나중에 종료
- child가 먼저 종료하고 parent가 나중에 wait
- parent가 wait하지 않고 먼저 종료
- child 생성 실패
- child load/fork 복제 실패

따라서 record 해제 정책이 필요하다. 추천은 ref count다.

```text
record 생성 시 ref_cnt = 2
  - parent reference 1
  - child reference 1

parent가 wait 성공 또는 parent exit로 record를 더 이상 보지 않으면 ref_cnt--
child가 exit로 자기 보고를 끝내면 ref_cnt--
ref_cnt == 0이면 free
```

ref count 대신 `orphan` flag를 써도 되지만, 어떤 경로에서 누가 free하는지 명확히 문서화해야 한다.

## 9. 구현 순서 추천

### 9.1 0단계: 헤더와 빈 구현부터 만들기

첫 번째 PR 또는 첫 번째 공동 작업은 기능 완성이 아니라 compile 가능한 토대를 만드는 것이다.

목표:

- `include/userprog/uaccess.h` 생성
- `userprog/uaccess.c` 생성
- `include/userprog/fd.h` 생성
- `userprog/fd.c` 생성
- `userprog/targets.mk`에 새 `.c` 파일 등록
- `thread.h`에 FD 필드 추가
- `syscall.h`에 공용 exit helper와 `filesys_lock` 선언 추가 여부 결정

이 단계에서는 내부 구현이 일부 TODO여도 된다. 단, 함수 이름과 시그니처는 최종 형태로 고정한다.

### 9.2 1단계: uaccess 먼저 완성

우선순위가 높은 이유:

- 파일 syscall도 filename pointer가 필요하다.
- exec/fork도 user string pointer가 필요하다.
- bad pointer 테스트가 kernel panic 방지의 핵심이다.

완료 기준:

- `validate_user_read/write`가 모든 page를 검사한다.
- `copy_in/copy_out`이 검증 후 복사한다.
- `copy_in_string`이 page boundary 문자열을 처리한다.
- invalid pointer에서 공용 exit 경로로 종료한다.

### 9.3 2단계: fd table 완성

uaccess가 준비되면 파일 syscall 담당자는 fd table을 구현한다.

완료 기준:

- `fd_init`이 process 시작 시 호출된다.
- `fd_alloc`은 2 이상 fd만 반환한다.
- `fd_get`은 invalid fd에 `NULL`을 반환한다.
- `fd_close`는 invalid fd에도 no-op이다.
- `fd_duplicate_all`은 `file_duplicate`로 복제한다.

### 9.4 3단계: dispatcher와 파일 syscall 연결

파일 syscall은 다음 구조로 연결한다.

```text
syscall_handler
  -> syscall_file_create/remove/open/read/write...
     -> uaccess로 pointer 복사/검증
     -> filesys_lock
     -> filesys/file 호출
     -> fd API 사용
```

가능하면 파일 syscall 구현을 `userprog/syscall_file.c`로 분리하면 `syscall.c` 충돌이 줄어든다. 단, 이 경우 header도 함께 정해야 한다.

### 9.5 4단계: fork/exec/wait/exit 연결

process lifecycle 담당자는 다음 순서로 연결하는 것이 좋다.

1. `exit(status)`가 status 저장, fd 정리, child status notify를 하게 만든다.
2. `wait(pid)`가 direct child record만 기다리게 만든다.
3. `fork(name)`가 child status record를 만들고 child 복제 성공을 parent에게 알려주게 만든다.
4. `fd_duplicate_all()`을 fork에 연결한다.
5. `exec(cmd_line)`에서 `copy_in_string()`과 `process_exec()`를 연결한다.
6. rox를 위해 `running_file`과 `file_deny_write()`를 연결한다.

## 10. 담당자별 작업 규칙

### 10.1 1번 담당: uaccess

해야 할 일:

- `uaccess.h/c` 구현
- invalid pointer 실패 정책 구현
- page boundary 검증
- writable PTE 검증

하지 말아야 할 일:

- 파일 syscall 내부 정책을 직접 구현하지 않는다.
- fd table 구조를 직접 만지지 않는다.
- `filesys_lock`을 잡지 않는다.

다른 담당자에게 제공하는 것:

- 안전한 user pointer 검증 함수
- 안전한 user/kernel 복사 함수
- user string 복사 함수

### 10.2 2번 담당: fd와 파일 syscall

해야 할 일:

- `fd.h/c` 구현
- 파일 syscall 구현
- `thread.h`의 fd 필드 관리
- file syscall에서 `filesys_lock` 사용

하지 말아야 할 일:

- syscall dispatcher 전체를 독점적으로 크게 바꾸지 않는다.
- fork/wait child status record를 설계하지 않는다.
- user pointer를 직접 검사하지 않는다.

다른 담당자에게 제공하는 것:

- fd table 초기화/할당/조회/닫기
- fork용 fd table 복제
- exit용 fd 전체 정리

### 10.3 3번 담당: dispatcher와 lifecycle

해야 할 일:

- `syscall.c` dispatcher 완성
- `fork/exec/wait/exit` 구현
- child status record 설계
- rox 구현
- `process_wait()` 정식 구현

하지 말아야 할 일:

- fd table 내부 배열을 직접 순회하지 않는다. 필요한 경우 `fd` API를 확장한다.
- user pointer를 직접 역참조하지 않는다.
- exit message를 여러 곳에서 출력하지 않는다.

다른 담당자에게 기대하는 것:

- user pointer는 `uaccess`가 안전하게 처리한다.
- fd 복제와 정리는 `fd` API가 처리한다.

## 11. syscall별 공통 API 적용 예시

### 11.1 create

```text
1. file pointer를 copy_in_string()으로 kernel string으로 복사
2. filesys_lock acquire
3. filesys_create(kernel_file, initial_size)
4. filesys_lock release
5. kernel string 해제
6. bool 반환
```

### 11.2 open

```text
1. file pointer를 copy_in_string()으로 kernel string으로 복사
2. filesys_lock acquire
3. filesys_open(kernel_file)
4. filesys_lock release
5. 성공하면 fd_alloc(file)
6. fd_alloc 실패 시 file_close(file)
7. kernel string 해제
8. fd 또는 -1 반환
```

### 11.3 read

```text
1. size == 0이면 0 반환
2. buffer는 validate_user_write(buffer, size)
3. fd == 0이면 input_getc()로 읽고 copy_out 또는 직접 저장
4. fd == 1이면 -1 반환
5. 일반 fd는 fd_get(fd)
6. invalid이면 -1 반환
7. filesys_lock acquire
8. file_read()
9. filesys_lock release
10. 읽은 byte 수 반환
```

### 11.4 write

```text
1. size == 0이면 0 반환
2. buffer는 validate_user_read(buffer, size)
3. fd == 1이면 putbuf(buffer, size), size 반환
4. fd == 0이면 -1 반환
5. 일반 fd는 fd_get(fd)
6. invalid이면 -1 반환
7. filesys_lock acquire
8. file_write()
9. filesys_lock release
10. 쓴 byte 수 반환
```

### 11.5 exec

```text
1. cmd_line pointer를 copy_in_string()으로 kernel string으로 복사
2. process_exec(kernel_cmd_line)
3. 성공하면 돌아오지 않음
4. 실패하면 exit(-1)
```

`process_exec()`가 넘겨받은 page를 해제하는지, caller가 해제하는지는 팀에서 한쪽으로 고정해야 한다. 현재 Pintos skeleton은 `process_exec()` 내부에서 `f_name` page를 해제하는 흐름이므로, 그 정책을 유지할지 먼저 확인한다.

### 11.6 fork

```text
1. thread_name을 copy_in_string()으로 복사
2. child_status record 생성
3. parent children list에 등록
4. process_fork(name, intr_frame) 호출
5. child가 address space와 fd table 복제 완료할 때까지 parent 대기
6. 성공하면 child tid 반환
7. 실패하면 -1 반환 및 record 정리
```

fork는 `uaccess`, `fd`, `process lifecycle`이 모두 만나는 지점이다. 따라서 fork 담당자는 fd table 내부를 직접 복제하지 말고 `fd_duplicate_all()`을 호출한다.

### 11.7 exit

```text
1. exit status 저장
2. exit message 출력
3. fd_close_all()
4. running_file close
5. child_status에 status 기록
6. wait_sema up
7. child list orphan 처리
8. thread_exit()
```

비정상 종료도 이 경로를 타야 한다. 그래야 invalid pointer로 죽은 child에 대해 `wait()`가 `-1`을 받을 수 있다.

## 12. 테스트와 통합 순서

### 12.1 API compile

먼저 새 헤더와 새 `.c` 파일이 빌드에 들어가는지 확인한다.

```sh
cd pintos/userprog
make
```

이 단계에서는 모든 테스트 통과가 목표가 아니다. 새 인터페이스 이름이 서로 맞고 링크되는지가 목표다.

### 12.2 pointer robustness

`uaccess` 구현 후 우선 확인할 테스트:

```text
create-bad-ptr
open-bad-ptr
read-bad-ptr
write-bad-ptr
create-bound
open-boundary
read-boundary
write-boundary
bad-read
bad-write
bad-read2
bad-write2
bad-jump
bad-jump2
```

### 12.3 fd와 파일 syscall

fd 구현 후 우선 확인할 테스트:

```text
open-normal
open-twice
open-missing
read-normal
read-zero
read-bad-fd
write-normal
write-zero
write-bad-fd
close-normal
close-twice
close-bad-fd
```

### 12.4 lifecycle

fork/wait/exit 구현 후 우선 확인할 테스트:

```text
wait-simple
wait-twice
wait-bad-pid
wait-killed
fork-once
fork-read
fork-close
exec-once
exec-read
rox-simple
```

## 13. 리뷰 체크리스트

공통 인터페이스 구현 PR을 리뷰할 때는 다음을 확인한다.

- `uaccess.h`의 함수 이름과 시그니처가 분업 가이드와 같다.
- `fd.h`의 함수 이름과 시그니처가 분업 가이드와 같다.
- 새 `.c` 파일이 `userprog/targets.mk`에 등록되어 있다.
- user pointer 검증 중 `filesys_lock`을 잡지 않는다.
- `validate_user_write()`가 writable PTE를 확인한다.
- `copy_in_string()`의 반환 buffer 소유권이 문서화되어 있다.
- fd `0`, `1`은 예약되어 있다.
- invalid fd에서 panic이 나지 않는다.
- `fd_duplicate_all()` 실패 시 부분 복제 자원을 정리한다.
- exit message 출력 위치가 하나로 정해져 있다.
- child status record가 child thread 수명에 의존하지 않는다.
- parent가 wait하지 않고 죽는 경우 record 수명 정책이 있다.

## 14. 팀 합의용 최종 질문

구현을 시작하기 전에 팀에서 아래 질문에 답을 적어 두면 좋다.

```text
1. invalid pointer에서 호출할 공용 함수 이름은 무엇인가?
2. copy_in_string()의 최대 문자열 길이는 얼마인가?
3. copy_in_string() 반환 buffer는 palloc page인가 malloc buffer인가?
4. fd_alloc() 실패 시 file_close() 책임은 caller인가 fd_alloc()인가?
5. filesys_lock은 어느 파일에서 정의하고 어느 초기화 함수에서 lock_init 하는가?
6. exit message는 syscall_exit()에서 출력하는가 process_exit()에서 출력하는가?
7. child_status record의 필드 이름과 ref count 정책은 무엇인가?
8. process_exec()에 넘긴 command line page는 누가 해제하는가?
9. running_file은 exec 성공 직후 어디에서 저장하고 exit에서 어디에서 닫는가?
10. 파일 syscall 구현은 syscall.c에 둘 것인가 syscall_file.c로 분리할 것인가?
```

이 질문에 대한 답이 정해지면 공통 인터페이스 계약은 “문서상 아이디어”가 아니라 실제 병렬 개발이 가능한 기준선이 된다.
