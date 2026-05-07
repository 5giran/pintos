# Pintos P2 User Memory + Syscall 3인 병렬 분업 가이드

## 0. 목표와 전제

이 문서는 Pintos Project 2의 `user memory access`와 기본 `syscall` 구현을 3명이 최대한 병렬로 진행하기 위한 팀 분업 가이드다. 팀원들이 이 문서만 보고 바로 작업을 시작할 수 있도록, 역할별 책임, 공동 결정 사항, 테스트 범위, 위험 요소를 함께 정리한다.

전제는 다음과 같다.

- `argument-passing`은 이미 구현되어 있다.
- `process_wait()`은 현재 임시 구현 상태이므로, 이번 분업에서 정식 구현까지 포함한다.
- Extra인 `dup2`는 이번 범위에서 제외한다.
- 목표는 기본 `tests/userprog`, `tests/userprog/no-vm`, `tests/filesys/base` 테스트 통과다.
- 기존 분업 구조는 유지하되, `process_wait()` 정식 구현은 3번 담당의 process lifecycle 범위에 추가한다.

핵심 원칙은 한 파일에 세 명이 동시에 달라붙지 않는 것이다. `syscall.c`, `process.c`, `thread.h`는 충돌이 쉬우므로 소유자를 정하고, 나머지는 독립 파일로 분리한다.

## 1. 왜 이렇게 나누는가

이번 과제는 syscall 개수만 보고 나누면 병렬성이 낮다. 이유는 거의 모든 syscall이 user pointer 검증을 필요로 하고, fork는 user memory, page table, fd table, parent-child sync를 모두 필요로 하기 때문이다.

그래서 분업 기준은 다음 세 가지다.

- 공통 안전 레이어: 모든 syscall이 사용하는 user pointer 검증과 복사
- 반복형 파일 기능: fd table과 파일 syscall
- 통합형 프로세스 기능: syscall dispatcher, fork, exec, wait, exit, rox

이렇게 나누면 1번과 2번은 거의 독립적으로 진행할 수 있고, 3번도 공통 API 이름만 먼저 고정하면 초반부터 병렬로 작업할 수 있다. 완전한 100% 병렬은 불가능하지만, 약 70~80%는 병렬 진행이 가능하다.

## 2. 전체 병렬 진행 구조

### 진행 순서

1. 30분 공동 설계로 공통 인터페이스를 확정한다.
2. 1번 담당자는 사용자 메모리 접근 레이어를 구현한다.
3. 2번 담당자는 FD 테이블과 파일 syscall 구현을 진행한다.
4. 3번 담당자는 syscall dispatcher, fork, exec, wait, exit, rox를 진행한다.
5. 마지막에 셋이 함께 통합한다.

### 병렬 불가능한 공동 결정과 이유

다음 항목은 시작 전에 반드시 함께 정해야 한다.

- `uaccess` API 이름과 실패 정책
- `fd` API 이름과 fd table 크기
- `struct thread`에 추가할 필드 이름
- `syscall.c` 최종 dispatcher 소유자
- invalid pointer 처리 방식
- invalid fd 처리 방식
- `filesys_lock` 사용 규칙
- child status record 구조와 wait/exit 상태 저장 정책

이 부분은 병렬로 나누면 안 된다. 세 파트가 모두 같은 함수와 필드 이름을 호출하기 때문이다. 여기서 이름이 다르면 나중에 빌드 충돌이 크게 나고, 각 담당자가 만든 코드가 서로 맞물리지 않는다.

특히 wait 관련 자료구조는 `fork`, `exit`, `process_wait()`가 같은 record를 공유한다. 따라서 wait만 별도 담당으로 빼지 않는다. 별도 담당으로 나누면 parent-child 관계 저장, exit status 저장, semaphore wakeup, record 해제 정책이 서로 어긋나기 쉽다.

## 3. 공통 인터페이스 계약

### 3.1 사용자 메모리 API

1번 담당자가 구현하고, 2번/3번 담당자는 이 함수만 사용한다.

```c
/* include/userprog/uaccess.h */

void validate_user_read (const void *uaddr, size_t size);
void validate_user_write (void *uaddr, size_t size);

void copy_in (void *dst, const void *usrc, size_t size);
void copy_out (void *udst, const void *src, size_t size);

/* 호출자가 palloc/free 가능한 kernel buffer를 받는다. */
char *copy_in_string (const char *ustr);
```

정책은 다음과 같다.

- `NULL`, kernel address, unmapped address, overflow range는 현재 프로세스를 `exit(-1)`로 종료한다.
- `size == 0`인 buffer 검사는 성공 처리한다.
- 문자열은 user memory에서 kernel memory로 복사한 뒤 syscall 로직에 넘긴다.
- `read(fd, buffer, size)`의 buffer는 write 대상이므로 writable 검사를 해야 한다.
- `write(fd, buffer, size)`의 buffer는 read 대상이므로 readable 검사만 한다.
- user pointer 검증 중에는 `filesys_lock`을 잡지 않는다.

### 3.2 FD API

2번 담당자가 구현하고, 3번 담당자는 fork/exit에서 이 API만 사용한다.

```c
/* include/userprog/fd.h */

#define FD_MAX 128

void fd_init (struct thread *t);
int fd_alloc (struct file *file);
struct file *fd_get (int fd);
void fd_close (int fd);
void fd_close_all (void);
bool fd_duplicate_all (struct thread *dst, struct thread *src);
```

정책은 다음과 같다.

- fd `0`은 stdin, fd `1`은 stdout 예약이다.
- `open()`은 성공 시 항상 `2` 이상을 반환한다.
- 일반 파일 fd가 invalid이면 `read/write/filesize/tell`은 `-1`을 반환한다.
- `close()`는 invalid fd를 받아도 커널 panic 없이 종료한다.
- fork 시 parent와 child는 같은 `struct file *`를 공유하지 않고 `file_duplicate()`로 복제한다.

### 3.3 `struct thread` 추가 필드

FD 관련 필드는 2번 담당자가 추가한다.

```c
#ifdef USERPROG
	struct file *fd_table[FD_MAX];
	int next_fd;
	struct file *running_file;
#endif
```

`running_file`은 실행 중인 executable의 쓰기를 막기 위해 사용한다.

wait/process lifecycle 관련 필드는 3번 담당자가 추가한다. 같은 `thread.h`를 만지므로, 실제 작업 전에는 2번 담당자와 필드 위치를 맞춘다.

필요한 개념은 다음과 같다.

- 현재 프로세스가 만든 direct child 목록
- 현재 프로세스가 부모에게 보고할 child status record 포인터
- 현재 프로세스의 exit status

child status record는 child가 종료된 뒤에도 parent가 `wait()`로 status를 회수할 때까지 살아 있어야 한다. 따라서 `struct thread` 안에만 저장하면 안 되고, 부모/자식 수명보다 오래 버틸 수 있는 별도 동적 구조체로 관리한다.

## 4. 1번 담당: User Memory / Pointer Safety

### 책임

사용자 프로그램이 넘긴 포인터를 커널이 안전하게 읽고 쓸 수 있게 만드는 공통 레이어를 구현한다.

주 소유 파일은 다음과 같다.

- `userprog/uaccess.c`
- `include/userprog/uaccess.h`
- 필요 시 `userprog/exception.c`

### 구현 내용

`validate_user_read()`:

- user buffer가 읽기 가능한지 검사한다.
- page boundary를 걸치는 buffer도 모든 page를 확인한다.
- 시작 주소와 마지막 주소만 검사하지 않는다.

`validate_user_write()`:

- user buffer가 쓰기 가능한지 검사한다.
- mapped 여부뿐 아니라 writable PTE 여부도 확인한다.
- `read()` syscall의 destination buffer 검증에 사용한다.

`copy_in()`:

- user buffer에서 kernel buffer로 복사한다.
- 복사 전에 readable 검증을 수행한다.

`copy_out()`:

- kernel buffer에서 user buffer로 복사한다.
- 복사 전에 writable 검증을 수행한다.

`copy_in_string()`:

- user string을 kernel buffer로 복사한다.
- `create/open/remove/exec/fork`에서 filename, cmdline, thread name 복사에 사용한다.
- 문자열이 page boundary를 걸쳐도 정상 처리한다.
- invalid pointer 발견 시 현재 프로세스를 `exit(-1)`로 종료한다.

### 알아야 할 배경

문서상 user memory 접근 과제의 핵심은 syscall 인자로 전달된 포인터가 잘못되었을 때 OS가 panic나지 않고 해당 user process만 죽이는 것이다. 잘못된 포인터에는 `NULL`, kernel address, unmapped address, 일부만 valid한 buffer가 포함된다.

현재 `syscall.c`의 단순한 `check_address()` 방식처럼 시작 주소와 마지막 주소만 확인하면 부족하다. buffer가 여러 page를 걸칠 때 중간 page가 unmapped일 수 있기 때문이다.

### 위험 요소와 주의점

- 시작 주소와 마지막 주소만 검사하면 `read-boundary`, `write-boundary`, `open-boundary` 계열에서 취약하다.
- `read()` buffer는 커널이 user memory에 쓰는 방향이다. writable 검사가 빠지면 read-only page에 쓰다가 kernel context page fault가 날 수 있다.
- user pointer 검증 중 `filesys_lock`을 잡으면 안 된다. invalid pointer로 종료될 때 lock leak이 생길 수 있다.
- page fault handler가 불필요한 디버그 출력을 남기면 테스트 expected output이 깨질 수 있다.
- `size == 0`인 read/write는 buffer를 건드리지 않고 0을 반환해야 한다.

### 담당 테스트

1번 담당자가 우선 통과시킬 테스트는 다음과 같다.

```text
create-bad-ptr
open-bad-ptr
exec-bad-ptr
read-bad-ptr
write-bad-ptr
create-bound
open-boundary
read-boundary
write-boundary
fork-boundary
exec-boundary
bad-read
bad-write
bad-read2
bad-write2
bad-jump
bad-jump2
```

단, `fork-boundary`와 `exec-boundary`는 3번 담당의 fork/exec 연결도 필요하므로 최종 판정은 통합 단계에서 한다. 1번 담당의 책임은 boundary 문자열과 buffer를 안전하게 검증/복사하는 것이다.

## 5. 2번 담당: FD Table / File Syscalls

### 책임

프로세스별 file descriptor table과 파일 관련 syscall을 구현한다.

주 소유 파일은 다음과 같다.

- `userprog/fd.c`
- `include/userprog/fd.h`
- `include/threads/thread.h`의 FD 필드
- 가능하면 파일 syscall 구현을 `userprog/syscall_file.c`로 분리

### 구현할 syscall

```text
create
remove
open
filesize
read
write
seek
tell
close
```

### 구현 내용

`create(file, initial_size)`:

- filename은 `copy_in_string()`으로 kernel에 복사한다.
- `filesys_create()`를 호출한다.
- 성공하면 `true`, 실패하면 `false`를 반환한다.

`remove(file)`:

- filename은 `copy_in_string()`으로 kernel에 복사한다.
- `filesys_remove()`를 호출한다.

`open(file)`:

- filename은 `copy_in_string()`으로 kernel에 복사한다.
- `filesys_open()`을 호출한다.
- 성공하면 fd table에 등록하고 fd를 반환한다.
- 실패하면 `-1`을 반환한다.
- fd `0`, `1`은 반환하지 않는다.

`filesize(fd)`:

- fd 조회 후 `file_length()`를 호출한다.
- invalid fd면 `-1`을 반환한다.

`read(fd, buffer, size)`:

- `size == 0`이면 0을 반환한다.
- fd `0`이면 `input_getc()`로 읽어 user buffer에 복사한다.
- fd `1`이면 `-1`을 반환한다.
- 일반 fd면 `file_read()`를 호출한다.
- buffer는 1번 담당의 writable 검증을 거친다.

`write(fd, buffer, size)`:

- `size == 0`이면 0을 반환한다.
- fd `1`이면 `putbuf()` 후 size를 반환한다.
- fd `0`이면 `-1`을 반환한다.
- 일반 fd면 `file_write()`를 호출한다.
- buffer는 1번 담당의 readable 검증을 거친다.

`seek(fd, position)`:

- valid file fd면 `file_seek()`를 호출한다.
- invalid fd면 아무 일도 하지 않는다.

`tell(fd)`:

- valid file fd면 `file_tell()`을 반환한다.
- invalid fd면 `-1`을 반환한다.

`close(fd)`:

- valid file fd면 `file_close()` 후 table slot을 비운다.
- invalid fd면 커널 panic 없이 종료한다.

### 동기화

- `filesys_*`, `file_*` 호출은 전역 `filesys_lock`으로 감싼다.
- user memory 검증과 복사는 lock 밖에서 한다.
- `process_exec()`도 파일 시스템에 접근하므로 3번 담당과 같은 `filesys_lock`을 사용해야 한다.

### 알아야 할 배경

Pintos의 파일 시스템 코드는 여러 thread가 동시에 진입해도 안전하다고 가정하면 안 된다. 문서에서도 `filesys` 디렉터리의 코드를 critical section으로 취급하라고 되어 있다. 따라서 파일 syscall과 `process_exec()`는 같은 lock 규칙을 따라야 한다.

fd table은 process별 자원이다. 전역 fd table을 쓰면 다른 process의 fd가 서로 섞인다. 특히 fork/exec 테스트에서 부모와 자식의 fd 관계가 중요하다.

### 위험 요소와 주의점

- fd table을 전역으로 두면 안 된다.
- fork 후 parent/child가 같은 `struct file *`를 공유하면 `fork-close`, `fork-read`, `exec-read`가 깨진다.
- `open()`이 fd `0` 또는 `1`을 반환하면 안 된다.
- `close-twice`, `close-bad-fd`에서 kernel panic이 나면 안 된다.
- `read-stdout`, `write-stdin`은 실패값 반환 또는 정상 종료 허용 범위 안에서 처리한다.
- rox 테스트에서 실행 파일에 대한 `write()`는 `0`이 나와야 한다. 이는 3번 담당의 `file_deny_write()`와 연결된다.

### 담당 테스트

2번 담당자가 우선 통과시킬 테스트는 다음과 같다.

```text
create-empty
create-long
create-normal
create-exists
create-null
open-missing
open-normal
open-twice
open-empty
open-null
read-normal
read-zero
read-stdout
read-bad-fd
write-normal
write-zero
write-stdin
write-bad-fd
close-normal
close-twice
close-bad-fd
```

파일 시스템 회귀 테스트는 다음과 같다.

```text
tests/filesys/base/sm-create
tests/filesys/base/sm-full
tests/filesys/base/sm-random
tests/filesys/base/sm-seq-block
tests/filesys/base/sm-seq-random
tests/filesys/base/lg-create
tests/filesys/base/lg-full
tests/filesys/base/lg-random
tests/filesys/base/lg-seq-block
tests/filesys/base/lg-seq-random
tests/filesys/base/syn-read
tests/filesys/base/syn-write
tests/filesys/base/syn-remove
```

## 6. 3번 담당: Dispatcher / Fork / Exec / Wait / Exit / Rox

### 책임

syscall dispatcher와 process lifecycle 관련 syscall을 구현한다. 기존 분업은 유지하되, 현재 임시 구현 상태인 `process_wait()`의 정식 구현까지 이 담당 범위에 포함한다.

주 소유 파일은 다음과 같다.

- `userprog/syscall.c`
- `userprog/process.c`
- 필요 시 `include/userprog/syscall.h`

### 구현할 syscall

```text
halt
exit
fork
exec
wait
```

파일 syscall은 2번 담당 함수로 위임한다. 3번 담당자가 dispatcher의 최종 소유자다.

`wait`를 3번 담당에 포함하는 이유는 다음과 같다.

- `fork`가 child status record를 생성해야 한다.
- `exit`가 같은 record에 exit status를 기록해야 한다.
- `process_wait()`가 같은 record를 찾아 block/unblock과 해제를 처리해야 한다.
- 세 기능이 모두 `process.c`, `thread.h`, parent-child lifecycle을 공유하므로 따로 나누면 병렬성보다 충돌 비용이 더 크다.

### Dispatcher 규칙

- syscall 번호는 `f->R.rax`에 있다.
- syscall 인자는 순서대로 `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`에 있다.
- 반환값은 `f->R.rax`에 저장한다.
- 알 수 없는 syscall은 현재 프로세스를 `exit(-1)`로 종료한다.

### `exit(status)`

- exit status를 저장한다.
- user process일 때만 `"%s: exit(%d)\n"` 형식으로 출력한다.
- 열린 fd를 `fd_close_all()`로 정리한다.
- `running_file`을 닫아 executable deny-write를 해제한다.
- 현재 프로세스의 child status record에 exit status와 종료 여부를 기록한다.
- parent가 기다릴 수 있도록 child status record의 semaphore를 `up`한다.
- parent가 먼저 죽은 경우에도 child status record가 안전하게 해제되도록 reference count 또는 동등한 orphan 처리 정책을 사용한다.
- `thread_exit()`로 종료한다.
- exit message가 중복 출력되지 않게 출력 위치를 하나로 고정한다.

### `fork(thread_name)`

- `thread_name`은 `copy_in_string()`으로 kernel에 복사한다.
- child status record를 생성하고 parent의 direct child 목록에 등록한다.
- parent `intr_frame`을 child에게 안전하게 전달한다.
- child는 parent register를 복사하되 `RAX = 0`으로 설정한다.
- parent는 child가 page table과 fd table 복제에 성공했는지 확인한 뒤 반환한다.
- child 복제 실패 시 parent의 fork는 `TID_ERROR` 또는 `-1`을 반환한다.
- child 생성 또는 resource 복제 실패 시 생성한 child status record도 정리한다.

### `duplicate_pte()`

- kernel virtual address는 skip한다.
- `palloc_get_page(PAL_USER)`로 child page를 할당한다.
- parent page 내용을 `memcpy()`한다.
- writable bit를 보존한다.
- 실패 시 할당한 page를 해제한다.

### `exec(cmd_line)`

- user cmdline을 `copy_in_string()`으로 kernel page에 복사한다.
- `process_exec()`에 넘긴다.
- 성공하면 돌아오지 않는다.
- 실패하면 현재 프로세스를 `exit(-1)`로 종료한다.
- fd table은 exec 후에도 유지한다.
- thread name은 exec 후에도 바꾸지 않는다.

### `wait(pid)`

- 임시 busy-wait/count loop 구현을 정식 `process_wait(pid)`로 교체한다.
- `pid`가 현재 프로세스의 direct child가 아니면 즉시 `-1`을 반환한다.
- 같은 child에 이미 wait한 적이 있으면 즉시 `-1`을 반환한다.
- child가 아직 살아 있으면 child status record의 semaphore에서 block한다.
- child가 이미 종료되어 있으면 block하지 않고 저장된 exit status를 반환한다.
- child가 exception, invalid pointer, load failure 등으로 정상 `exit()` 없이 죽은 경우 `-1`을 반환한다.
- wait 성공 후 child status record를 parent child list에서 제거하고 parent 쪽 reference를 해제한다.
- parent가 wait하지 않고 종료해도 child status record가 dangling pointer로 남지 않게 정리한다.

### `rox`

- `load()`에서 executable open 성공 후 `file_deny_write()`를 호출한다.
- 성공한 load에서는 executable file을 닫지 않고 `thread_current()->running_file`에 저장한다.
- 프로세스 종료 또는 다음 exec 전에 기존 `running_file`을 닫는다.

### 알아야 할 배경

fork는 단순히 thread를 하나 만드는 syscall이 아니다. child는 parent의 user memory와 fd table을 복제해야 하며, child의 반환값은 0이어야 한다. 또한 parent는 child가 복제에 성공했는지 알기 전에는 fork에서 반환하면 안 된다.

exec는 현재 프로세스를 새 프로그램으로 바꾸는 작업이다. 성공하면 반환하지 않고, 실패하면 현재 프로세스가 `exit(-1)`로 종료되어야 한다. exec 후에도 기존 fd table은 유지되어야 한다.

wait는 단순 delay가 아니다. parent가 만든 direct child의 종료 상태를 정확히 한 번 회수하는 기능이다. child가 parent보다 먼저 죽을 수도 있고, parent가 child보다 먼저 죽을 수도 있으며, parent가 wait하지 않을 수도 있다. 따라서 child status record는 parent와 child의 수명 차이를 견딜 수 있게 설계해야 한다.

rox는 실행 중인 executable 파일에 대한 write를 막는 기능이다. `file_deny_write()`만 호출하고 파일을 바로 닫으면 deny가 풀리므로, process가 살아 있는 동안 파일을 계속 열어 두어야 한다.

### 위험 요소와 주의점

- `process_fork()`에서 parent `intr_frame`을 제대로 넘기지 않으면 child context가 깨진다.
- child `RAX = 0`을 빼먹으면 parent/child 분기가 모두 틀어진다.
- parent가 child 복제 성공 전에 fork에서 반환하면 fork 실패를 감지하지 못한다.
- wait를 busy loop로 처리하면 CPU를 낭비하고, child exit status를 정확히 전달하지 못하며, `wait-twice`, `wait-killed`, `multi-oom`에서 깨진다.
- child status record를 `struct thread` 내부 주소에만 의존하면 child thread가 파괴된 뒤 parent가 dangling pointer를 볼 수 있다.
- parent exit 시 child list를 정리하지 않으면 orphan child가 종료할 때 이미 죽은 parent 구조를 건드릴 수 있다.
- exec 실패 후 이미 address space를 cleanup했다면 복구하지 말고 `exit(-1)`로 종료하는 정책을 따른다.
- running executable file을 load 성공 직후 닫으면 deny-write가 풀려 rox가 실패한다.
- `multi-oom`은 resource leak을 잡는다. 비정상 종료에서도 fd, running file, child 상태가 정리되어야 한다.

### 담당 테스트

3번 담당자가 우선 통과시킬 테스트는 다음과 같다.

```text
halt
exit
fork-once
fork-multiple
fork-close
fork-read
fork-recursive
exec-once
exec-arg
exec-read
exec-boundary
exec-missing
exec-bad-ptr
wait-simple
wait-twice
wait-bad-pid
wait-killed
multi-recurse
multi-child-fd
rox-simple
rox-child
rox-multichild
```

resource leak 테스트는 다음과 같다.

```text
tests/userprog/no-vm/multi-oom
```

wait 구현 확인 순서는 다음을 추천한다.

```text
wait-simple      # 정상 child exit status 회수
wait-twice       # 같은 child에 대한 두 번째 wait는 -1
wait-bad-pid     # direct child가 아닌 pid는 -1
wait-killed      # 비정상 종료 child는 -1
multi-oom        # 반복 fork/exit에서 child status와 fd 누수 없음
```

## 7. 통합 순서

### 7.1 1차 통합: API compile

목표:

- `uaccess.h`, `fd.h` include 가능
- `targets.mk`에 새 `.c` 파일 등록
- 전체 빌드 성공

통과 기준:

```sh
cd pintos/userprog
make
```

### 7.2 2차 통합: 파일 syscall

목표:

- `create/open/read/write/close` 기본 기능 통과
- invalid fd에서 panic 없음

우선 실행:

```sh
make tests/userprog/create-normal.result
make tests/userprog/open-normal.result
make tests/userprog/read-normal.result
make tests/userprog/write-normal.result
make tests/userprog/close-normal.result
```

### 7.3 3차 통합: pointer robustness

목표:

- bad pointer와 boundary 테스트에서 kernel panic 없음

우선 실행:

```sh
make tests/userprog/create-bad-ptr.result
make tests/userprog/open-boundary.result
make tests/userprog/read-boundary.result
make tests/userprog/write-boundary.result
make tests/userprog/bad-read.result
```

### 7.4 4차 통합: fork/exec/wait/rox

목표:

- fork/exec/wait 연결
- FD 복제
- process_wait 정식 구현
- rox deny-write

우선 실행:

```sh
make tests/userprog/fork-once.result
make tests/userprog/fork-read.result
make tests/userprog/exec-once.result
make tests/userprog/exec-read.result
make tests/userprog/wait-simple.result
make tests/userprog/wait-twice.result
make tests/userprog/wait-bad-pid.result
make tests/userprog/wait-killed.result
make tests/userprog/rox-simple.result
```

### 7.5 최종 통합

최종 실행:

```sh
cd pintos/userprog
make check
```

통과 목표:

```text
tests/userprog/Rubric.functionality
tests/userprog/Rubric.robustness
tests/userprog/no-vm/Rubric
tests/filesys/base/Rubric
```

## 8. 작업 충돌 방지 규칙

- 1번 담당자는 `syscall.c`를 크게 수정하지 않는다.
- 2번 담당자는 dispatcher를 직접 완성하지 않고 파일 syscall 함수만 제공한다.
- 3번 담당자는 `syscall.c` dispatcher의 최종 소유자다.
- `thread.h`는 2번과 3번이 모두 만질 수 있으므로 시작 전에 필드 위치를 합의한다. 2번은 FD 필드, 3번은 process lifecycle/wait 필드만 추가한다.
- `process.c`는 3번 담당자가 소유한다. 2번 담당자는 필요한 fd helper만 제공한다.
- lock 이름은 하나만 쓴다. 예: `filesys_lock`.
- invalid pointer에서 종료되는 경로는 하나로 모은다. 여러 곳에서 직접 `printf` 후 `thread_exit()`하지 않는다.

## 9. 최종 체크리스트

- syscall 번호와 인자 register를 정확히 사용했다.
- 반환값은 `f->R.rax`에 저장한다.
- 모든 user pointer는 `uaccess` API를 거친다.
- user pointer 검증은 `filesys_lock` 밖에서 한다.
- fd `0`, `1`은 stdin/stdout 예약이다.
- `open()`은 fd `2` 이상만 반환한다.
- fork child는 `RAX = 0`이다.
- fork parent는 child 복제 성공을 기다린다.
- `process_wait()`은 direct child만 기다리고, 같은 child에 대한 두 번째 wait는 `-1`을 반환한다.
- child가 이미 죽었어도 parent는 저장된 exit status를 회수할 수 있다.
- 비정상 종료 child의 wait 결과는 `-1`이다.
- parent가 wait하지 않고 종료해도 child status record가 안전하게 정리된다.
- exec 성공 시 반환하지 않는다.
- exec 실패 시 현재 프로세스가 `exit(-1)`로 종료된다.
- exec 후에도 fd table은 유지된다.
- 실행 파일은 프로세스가 살아 있는 동안 닫지 않는다.
- exit message는 user process 종료 시 정확히 한 번만 출력된다.
- 디버그 출력은 최종 전에 제거한다.
