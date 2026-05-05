# PintOS Project 2 - 3번 담당 Debug Notes

## 배경

이 문서는 3번 담당 범위인 `Dispatcher / Fork / Exec / Exit / Rox`를 구현하면서 헷갈렸던 지점을 누적해서 정리한다.

현재는 `exit` 기본 구현, parent-child status record 초기 helper, `process_create_initd()`, `process_exit()`, `process_wait()`, ROX 구현 중 나온 질문을 정리한다. 이후 `fork`, `exec` syscall 경로 점검 중 나온 질문은 이 문서에 section을 추가한다.

## 1. user program의 `exit(57)`은 어떻게 `syscall_handler()`로 들어오는가

테스트 프로그램에는 다음처럼 보인다.

```c
exit (57);
```

하지만 이 `exit()`은 kernel 함수를 직접 호출하는 것이 아니다. 사용자 프로그램에 링크되는 `lib/user/syscall.c`의 wrapper 함수다.

흐름은 다음과 같다.

```text
user program
  exit(57) 호출

lib/user/syscall.c
  rax = SYS_EXIT
  rdi = 57
  syscall instruction 실행

CPU
  user mode에서 kernel mode로 전환

syscall-entry.S
  register들을 intr_frame에 저장

syscall_handler(f)
  f->R.rax로 syscall 번호 확인
  f->R.rdi에서 57 읽음
```

그래서 `SYS_EXIT` case에서 `(int) f->R.rdi`를 읽으면 user program이 넘긴 status를 얻는다.

## 2. `SYS_EXIT`와 `process_exit()`의 책임은 다르다

`SYS_EXIT`는 "현재 process가 어떤 status로 종료되는가"를 정하는 곳이다.

`process_exit()`은 실제 종료 정리를 하는 곳이다. 이후 여기에 fd 정리, 실행 파일 정리, parent-child 상태 기록, 부모 wake-up 같은 작업이 붙는다.

따라서 현재 구조는 이렇게 나누는 방향이다.

```text
SYS_EXIT
  current->exit_status = status
  thread_exit()

process_exit()
  exit message 출력
  process_cleanup()
```

출력 위치는 한 곳이어야 한다. 지금 방향에서는 `SYS_EXIT`에서 출력하지 않고 `process_exit()`에서 출력한다.

## 3. `exit_status`를 저장하는 이유

`process_exit()`은 status를 인자로 받지 않는다.

```c
void process_exit (void);
```

그런데 status는 두 곳에서 필요하다.

- exit message 출력
- 나중에 부모의 `wait(pid)` 반환값

그래서 `SYS_EXIT`에서 받은 값을 현재 thread의 `exit_status`에 저장한다.

```text
SYS_EXIT
  thread_current()->exit_status = status
  thread_exit()

process_exit()
  thread_current()->exit_status를 읽음
```

단, 최종 `wait` 구현에서는 이 값만으로 부족하다. 자식 thread가 사라진 뒤에도 부모가 status를 읽어야 하므로, 나중에는 parent-child 공유 구조에 복사해야 한다.

## 4. `thread_exit(status)`로 바꾸지 않는 이유

`thread_exit()`은 scheduler 관점에서 현재 thread를 끝내는 함수다. 반면 `exit_status`는 user process의 종료 상태다.

즉 책임이 다르다.

```text
thread_exit()
  thread lifecycle 담당

exit_status
  user process lifecycle 담당
```

그래서 `thread_exit()`의 시그니처를 바꾸기보다, USERPROG에서 의미 있는 상태인 `exit_status`를 `struct thread`에 두고 `process_exit()`이 읽는 방식이 더 자연스럽다.

## 5. 기본값 `-1`은 어디서 초기화하는가

`exit_status`는 새 thread가 만들어질 때 기본값을 가져야 한다. 그래서 `init_thread()`에서 `-1`로 초기화한다.

`pml4` 생성 시점에 같이 초기화하는 것은 책임이 맞지 않는다.

```text
pml4
  user address space 상태

exit_status
  process 종료 상태
```

둘은 같은 user process에서 쓰이지만 같은 종류의 상태는 아니다.

## 6. `process_exit()`의 출력 조건을 왜 두는가

PintOS Project 2에서는 user process 하나가 kernel thread 하나 위에서 실행된다. 그래서 사용자 프로그램을 실행하는 thread도 엄밀히 말하면 kernel thread다.

여기서 중요한 구분은 "kernel thread냐 아니냐"가 아니라, "이 thread가 user process 하나를 대표하고 있느냐"이다.

```text
user process를 대표하는 thread
  user program을 load해서 실행한다.
  pml4를 가진다.
  exit message 출력 대상이다.

PintOS 내부용 thread
  user program 하나를 대표하지 않는다.
  pml4가 없다.
  exit message 출력 대상이 아니다.
```

`thread_exit()`은 공통 thread 종료 경로이고, USERPROG 빌드에서는 그 안에서 `process_exit()`을 호출한다. 그래서 `process_exit()`이 무조건 exit message를 출력하면, user process를 대표하지 않는 thread의 종료에도 메시지가 찍힐 수 있다.

현재 구현에서 `curr->pml4 != NULL` 조건을 둔 이유는 이 구분을 하기 위해서다.

```text
curr->pml4 != NULL
  user process를 대표하는 thread로 보고 exit message 출력

curr->pml4 == NULL
  user process를 대표하지 않는 thread로 보고 출력하지 않음
```

이 조건은 현재 기본 구현에서 쓰는 실용적인 기준이다. 나중에 process 상태가 더 복잡해지면 별도 flag나 child status 구조와 함께 조정할 수 있다.

## 7. parent-child 관계는 `fork()`에서만 생기는가

항상 `fork()`에서만 생기는 것은 아니다.

`fork()`에서는 user process가 다른 user process를 만든다.

```text
user process parent
  fork()
  user process child 생성
  wait(child_tid)
```

하지만 첫 user process는 kernel 쪽에서 만들어진다.

```text
kernel main/init thread
  process_create_initd("args-single ...")
  initd user process thread 생성
  process_wait(initd_tid)
```

이 경우 parent는 user process가 아니라 PintOS 내부 kernel thread다. 그래도 "child를 만들고, 나중에 그 child의 종료를 기다린다"는 점은 같다.

따라서 child status record는 `fork()` 전용 자료구조가 아니다. `process_create_initd()`와 `process_fork()`처럼 wait 가능한 child를 만드는 경로에서 필요하다.

`exec()`는 child를 새로 만들지 않는다. 현재 process의 프로그램 이미지만 바꾸므로 parent-child record를 새로 만들지 않는다.

## 8. `struct thread`에는 무엇을 넣고, record는 어디에 두는가

`child_status` record 자체를 `struct thread` 안에 통째로 넣는 방식은 위험하다. child thread가 종료되면 `struct thread`도 사라질 수 있는데, parent가 나중에 `wait()`으로 status를 읽어야 할 수 있기 때문이다.

그래서 구조는 다음처럼 나눈다.

```text
struct thread
  children
    내가 만든 direct child들의 child_status record 목록

  child_status
    내가 parent에게 보고할 때 사용할 내 record pointer

struct child_status
  타입 정의는 thread.h에 둔다.
  record 객체는 process.c에서 child마다 malloc으로 별도 할당한다.
```

즉 `struct thread`에는 record를 찾기 위한 list와 pointer만 두고, record 자체는 parent thread와 child thread의 수명에 묶이지 않도록 별도 kernel memory로 관리한다.

### 8.1. `thread.h`에 구조체 정의를 둬도 되는가

처음에는 `thread.h`에 forward declaration만 두는 방향도 가능했다.

```c
struct child_status;
struct child_status *child_status;
```

이 방식은 `thread.h`가 record 내부 필드를 몰라도 될 때 쓸 수 있다. 포인터만 저장하려면 `struct child_status`라는 타입 이름만 알아도 충분하기 때문이다.

하지만 지금은 `struct child_status` 타입 정의를 `thread.h`로 옮겼다. 이것도 잘못된 방식이 아니다. 팀원들이 여러 파일에서 같은 child status record 타입을 볼 수 있고, `struct thread`와 밀접한 lifecycle record라는 점이 더 명확해진다.

여기서 헷갈리면 안 되는 구분은 다음이다.

```text
괜찮은 것
  struct child_status의 타입 정의를 thread.h에 둔다.
  parent와 child가 record pointer를 공유한다.
  실제 record 객체는 process.c에서 malloc으로 child마다 만든다.

위험한 것
  struct child_status record 객체를 struct thread 안에 값으로 통째로 넣는다.
```

문제는 "구조체 정의가 thread.h에 있느냐"가 아니다. 진짜 문제는 record 객체의 수명이 parent thread나 child thread 객체의 수명에 묶이는가다.

```text
타입 정의 위치
  컴파일러가 필드 구성을 알 수 있게 하는 위치

객체 저장 위치
  실제 record 메모리가 어디에 생기고 언제 free되는지의 문제
```

현재 구조는 타입 정의는 `thread.h`, 객체 생성/해제는 `process.c` helper가 담당하는 형태다.

`thread.h`가 `process.h`를 include할 필요는 없다. 현재 `process.h`는 이미 `thread.h`를 include하므로, 반대로 `thread.h`가 `process.h`를 include하면 header 의존성이 서로 물릴 수 있다.

`process.c`를 include하는 것도 하면 안 된다. `.c` 파일은 include 대상이 아니라 컴파일 대상이다. 공유해야 하는 것은 header에 두고, 실제 함수 구현은 `.c` 파일에 둔다.

참고로 `struct child_status` 안에 `struct semaphore wait_sema`를 값으로 넣었기 때문에, `thread.h`에서는 `struct semaphore`의 실제 크기를 알아야 한다. 그래서 `thread.h`가 `threads/synch.h`를 include한다.

## 9. `waited`는 현재 wait 중이라는 뜻인가

`waited` 또는 현재 코드의 `has_been_waited`는 "현재 wait 중인가"만 뜻하지 않는다.

더 정확한 의미는 다음이다.

```text
parent가 이 child에 대한 wait 권한을 이미 사용했는가
```

한 child는 정확히 한 번만 wait으로 회수할 수 있다.

```text
wait(child_tid)
  첫 번째 호출은 정상 회수 또는 대기 시작

wait(child_tid)
  두 번째 호출은 -1
```

따라서 `has_been_waited == true`는 두 상태를 모두 포함한다.

```text
child가 이미 죽어 있었고 parent가 status를 회수함
child가 아직 살아 있어서 parent가 wait에 들어갔고 회수 예정임
```

둘 다 같은 child에 대해 다시 wait하면 안 되는 상태다.

## 10. `child_status.elem`은 semaphore waiters용인가

아니다.

`child_status.elem`은 parent의 `children` list에 record를 매달기 위한 list element다.

```text
parent->children
  child_status A의 elem
  child_status B의 elem
  child_status C의 elem
```

반면 parent가 `sema_down(&child_status->wait_sema)`으로 잠들 때 semaphore waiters list에 들어가는 것은 parent thread의 `thread.elem`이다.

```text
child_status.elem
  parent->children list용

thread.elem
  ready_list, semaphore waiters 같은 thread 상태 list용
```

ready 상태와 semaphore block 상태는 동시에 일어나지 않으므로 PintOS는 `thread.elem`을 ready list와 semaphore waiters에 번갈아 쓴다. 하지만 parent의 `children` list는 thread 상태 list가 아니라 child status record들을 보관하는 list다. 그래서 별도의 `child_status.elem`이 필요하다.

## 11. 이 맥락의 semaphore는 lock과 같은 동기화인가

일반적으로 동기화는 두 용도로 쓰인다.

```text
1. 공유 자료를 동시에 건드리지 못하게 막는다.
2. 어떤 일이 일어날 때까지 기다리게 한다.
```

`wait_sema`는 주로 두 번째 용도다.

parent가 `wait()`을 호출했는데 child가 아직 살아 있으면 parent는 exit status를 읽을 수 없다. 그래서 parent는 semaphore에서 잠든다. child는 `process_exit()`에서 status를 기록한 뒤 `sema_up()`으로 parent를 깨운다.

```text
parent
  child_status->has_exited == false
  sema_down(&child_status->wait_sema)

child
  child_status->exit_status = status
  child_status->has_exited = true
  sema_up(&child_status->wait_sema)
```

그래도 공유 자료가 없는 것은 아니다. parent와 child는 같은 `child_status`를 본다.

```text
child가 쓰는 값
  exit_status
  has_exited

parent가 읽고 쓰는 값
  exit_status
  has_exited
  has_been_waited
```

`wait_sema`는 parent가 child보다 먼저 status를 읽지 않게 순서를 맞춰 주는 장치다.

## 12. record를 thread 안에 두면 왜 위험한가

이 문제는 두 단계로 나누어 봐야 한다.

```text
1단계
  record를 어디에 둘 것인가?

2단계
  별도로 둔 record를 언제 free할 것인가?
```

먼저 1단계는 record의 위치 문제다. record를 child thread 안에 두면 child가 먼저 죽을 때 위험하다.

```text
child thread 안에 record를 둠

child
  exit_status 기록
  종료

parent
  나중에 wait()
  읽을 record가 이미 사라졌을 수 있음
```

반대로 record를 parent thread 안에 두면 parent가 먼저 죽을 때 위험하다.

```text
parent thread 안에 record를 둠

parent
  child를 만들고 wait하지 않음
  exit

child
  나중에 exit
  status를 기록할 record가 이미 사라졌을 수 있음
```

그래서 record는 parent thread 안에도, child thread 안에도 통째로 두지 않는다. parent/child thread 바깥의 별도 kernel memory에 두고, parent와 child가 pointer로 같은 record를 참조하게 한다.

첫 user process만 생각하면 parent가 항상 기다리는 것처럼 보인다.

```text
kernel main/init thread
  process_create_initd(...)
  process_wait(initd_tid)
```

하지만 일반 `fork()` 관계에서는 parent가 반드시 wait하지 않는다.

```text
parent
  fork()
  wait하지 않고 exit

child
  나중에 exit
```

또 여러 child 중 일부만 wait하고 parent가 종료할 수도 있다. 따라서 "첫 user process는 기다린다"와 "모든 parent가 모든 child를 기다린다"는 다르다.

`fork()` 후 parent가 바로 exit하는 패턴이 항상 좋은 사용법은 아닐 수 있다. 새 child가 필요 없고 현재 process만 새 프로그램으로 바꾸려는 상황이라면 `exec()`가 더 맞다. 하지만 OS는 user program이 그런 패턴을 쓰지 않을 것이라고 가정하면 안 된다.

```text
fork
  child를 새로 만든다.
  parent가 wait하지 않고 먼저 죽을 수 있다.

exec
  현재 process의 프로그램만 바꾼다.
  child를 새로 만들지 않는다.
```

그래서 child status record는 특정 사용 패턴이 바람직한지와 별개로, 가능한 실행 순서를 모두 견디도록 parent/child 바깥의 별도 구조체로 관리해야 한다.

## 13. 별도 record에도 수명 정책이 필요한 이유

record를 별도 구조체로 뺐다고 문제가 끝나는 것은 아니다. 그 다음에는 이 별도 record를 언제 free할지 정해야 한다.

너무 빨리 free하면 다시 dangling pointer가 생긴다.

```text
child가 exit할 때 바로 free
  parent가 나중에 wait할 수 없음

parent가 exit할 때 바로 free
  child가 나중에 exit status를 기록할 수 없음
```

그래서 별도 record에는 reference count나 orphan 처리 같은 수명 정책이 필요하다.

핵심은 다음이다.

```text
parent가 더 이상 record를 쓰지 않음
child가 더 이상 record를 쓰지 않음
둘 다 확인된 뒤 free
```

정리하면 흐름은 다음과 같다.

```text
record를 thread 안에 두면 한쪽 thread 종료에 같이 사라질 수 있다.
  -> parent/child 바깥의 별도 구조체로 둔다.

별도 구조체도 아무 때나 free하면 한쪽이 아직 쓸 수 있다.
  -> 수명 정책을 둔다.
```

## 14. `child_status_create()`에서 메모리 할당이 왜 필요한가

`child_status` record는 `process_create_initd()`나 `process_fork()` 함수가 return한 뒤에도 parent와 child가 계속 참조해야 한다. 그래서 함수 안 지역 변수로 만들면 안 된다.

```text
지역 변수로 record 생성
  함수가 return하면 stack에서 사라짐
  parent->children 또는 child->child_status가 사라진 주소를 가리킬 수 있음
```

record는 child마다 하나씩 필요하므로 static/global record 하나로 두는 것도 안 된다. 여러 child가 같은 record를 덮어쓰게 된다.

그래서 `malloc()`으로 child마다 별도 record를 만든다. `palloc_get_page()`도 가능은 하지만 4KB page 단위라 작은 `child_status`에는 낭비가 크다.

```text
malloc(sizeof *cs)
  cs가 가리키는 struct child_status 하나의 크기만큼 메모리를 할당한다.
  반환된 주소를 cs에 저장한다.
```

`sizeof *cs`는 `sizeof(struct child_status)`와 같은 의미다. 처음에는 후자가 더 읽기 쉬울 수 있지만, `sizeof *cs`는 변수 타입이 바뀌어도 할당 크기가 같이 따라간다는 장점이 있다.

## 15. `static` 함수와 static 변수는 다르다

`static child_status_create(...)`처럼 함수 앞에 붙는 `static`은 "이 함수는 `process.c` 안에서만 쓴다"는 뜻이다. helper 함수의 공개 범위를 파일 내부로 제한하는 것이므로 괜찮다.

반대로 `static struct child_status record;`처럼 record 자체를 static 변수 하나로 만들면 안 된다. child마다 record가 따로 있어야 하는데 static record 하나는 모든 child가 공유하게 되기 때문이다.

정리하면 다음이다.

```text
static helper function
  process.c 내부 전용 함수라는 뜻
  사용 가능

static child_status variable
  record 하나를 계속 재사용한다는 뜻
  child별 record가 필요하므로 사용하면 안 됨
```

## 16. `TID_ERROR`는 무엇인가

`tid_t`는 thread id 타입이고 실제로는 정수다.

```text
tid_t
  thread id를 담는 int 계열 타입

TID_ERROR
  유효하지 않은 tid
  보통 -1
```

`thread_create()`가 실패하면 `TID_ERROR`를 반환한다. 처음에는 `child_status_create(tid)`가 정상 child tid를 받아야 한다고 생각했기 때문에 `tid == TID_ERROR`인 값으로 record를 만들면 안 된다고 봤다.

하지만 `process_create_initd()` 연결 중 `thread_create()` 전에 record를 만들어야 한다는 점이 확인되었다. 그래서 현재는 `child_status_create()`가 tid를 인자로 받지 않고, record 생성 직후에는 `cs->tid = TID_ERROR`를 임시값으로 둔다.

```text
record 생성 직후
  cs->tid = TID_ERROR

thread_create() 성공 후
  cs->tid = tid
```

즉 `TID_ERROR`는 지금 두 맥락에서 쓰인다.

```text
thread_create() 반환값이 TID_ERROR
  thread 생성 실패

cs->tid 초기값이 TID_ERROR
  아직 실제 child tid를 채우기 전의 임시값
```

`NULL`은 포인터에 쓰는 값이고, `tid`는 정수다. 그래서 `tid`를 검사할 때는 `NULL`이 아니라 `TID_ERROR`와 비교한다.

## 17. `child_status.exit_status`는 `thread.exit_status`와 중복인가

중복처럼 보이지만 역할이 다르다.

```text
thread.exit_status
  현재 child thread가 자기 종료 status를 들고 있는 곳

child_status.exit_status
  child가 죽은 뒤 parent가 wait()에서 읽을 수 있도록 보관하는 곳
```

child thread는 종료되면 사라질 수 있다. 따라서 parent가 나중에 `wait()`으로 status를 읽으려면 child status record 안에 status를 복사해 두어야 한다.

`has_exited`는 또 다른 의미다.

```text
has_exited
  child가 종료했는가?

exit_status
  child가 어떤 status로 종료했는가?
```

즉 `has_exited == true`가 된 뒤에야 `child_status.exit_status`를 parent가 의미 있게 읽을 수 있다.

## 18. `elem`은 왜 초기화하지 않는가

`struct list_elem elem`은 parent의 `children` list에 record를 연결하기 위한 고리다. `wait_sema`처럼 별도 init 함수로 초기화하는 필드가 아니다.

PintOS list 함수가 record를 list에 넣을 때 `elem.prev`, `elem.next`를 채운다.

```text
record 생성 직후
  elem은 아직 어떤 list에도 들어가지 않았음

list_push_back(&parent->children, &cs->elem)
  list 함수가 elem의 연결 정보를 채움
```

`elem.prev = NULL`, `elem.next = NULL`로 초기화할 수는 있지만 PintOS list API가 그 값을 보고 상태를 판단하지 않는다. 그래서 보통 하지 않는다.

중요한 규칙은 다음이다.

```text
struct list
  list_init() 필요

struct list_elem
  list에 삽입될 때 list 함수가 연결 정보를 세팅
```

## 19. `child_status_find()` 구현 중 놓친 부분

처음 구현한 방향은 맞았다. `parent->children` list를 순회하면서 `list_entry()`로 `child_status` record를 꺼내고, `cs->tid == child_tid`이면 그 record를 반환하는 구조였다.

처음 구현의 핵심 흐름은 다음과 같았다.

```c
struct list_elem *e = list_begin (&parent->children);

while (e != list_end (&parent->children)) {
  struct child_status *cs = list_entry (e, struct child_status, elem);
  if (cs->tid == child_tid) {
    return cs;
  }
  e = list_next (e);
}
```

여기서 빠진 것은 "못 찾았을 때의 반환값"이었다. 함수 반환 타입이 `struct child_status *`이므로, list 끝까지 순회했는데도 해당 tid를 못 찾으면 명확히 `NULL`을 반환해야 한다.

```c
return NULL;
```

이 `NULL`은 `process_wait()`에서 중요한 의미를 가진다.

```text
child_status_find(parent, child_tid) == NULL
  현재 thread의 direct child가 아님
  process_wait()은 -1 반환
```

또 처음 구상에서는 helper 안에서 `thread_current()`를 직접 사용할 수도 있었다. 하지만 현재 형태처럼 parent를 인자로 받는 편이 더 명확하다.

```text
child_status_find(child_tid)
  내부에서 thread_current()를 parent로 사용
  process_wait() 전용 helper에 가까움

child_status_find(parent, child_tid)
  어떤 parent의 children list를 검색하는지 호출부에서 명확히 보임
```

지금 구현은 후자 형태를 사용한다.

## 20. `child_status_release()`에서 주의할 점

`child_status_release(cs)`는 record를 무조건 free하는 함수가 아니다. 이 함수는 현재 caller가 들고 있던 참조 하나를 내려놓는다.

```text
ref_cnt--
ref_cnt == 0이면 free
```

처음 구현 방향은 맞았다. 여기에 추가로 필요한 안전장치는 두 가지다.

```text
cs == NULL이면 바로 return
  실수로 NULL이 들어와도 panic을 피한다.

ASSERT(cs->ref_cnt > 0)
  이미 release된 record를 또 release하는 버그를 잡는다.
```

가장 중요한 사용 규칙은 release 뒤에는 `cs`를 다시 만지면 안 된다는 것이다. release 호출에서 `free(cs)`가 되었을 수 있기 때문이다.

```text
release 전에 해야 하는 일
  status 읽기
  list_remove 하기
  sema_up 하기

release 후
  cs 접근 금지
```

이 helper는 나중에 세 흐름에서 쓰인다.

```text
child process_exit()
  child 몫 참조 release

parent process_wait()
  wait으로 status 회수 후 parent 몫 참조 release

parent process_exit()
  wait하지 않은 children list를 정리하면서 parent 몫 참조 release
```

## 21. `thread_create()` 뒤에 `child_status_create(tid)`를 하면 왜 위험한가

처음 생각한 흐름은 자연스러웠다.

```text
thread_create()로 child 생성
tid를 받음
child_status_create(tid)
parent->children에 등록
```

처음 코드도 이 방향에 가까웠다.

```c
tid = thread_create (thread_name, PRI_DEFAULT, initd, fn_copy);
struct child_status *cs = child_status_create (tid);
list_push_back (&(parent->children), &(cs->elem));
```

하지만 `thread_create()`는 child를 ready queue에 넣고 반환한다. 이때 scheduler가 child를 parent보다 먼저 실행할 수 있다. 심하면 parent가 `child_status_create(tid)`를 호출하기 전에 child가 `exit()`까지 갈 수 있다.

```text
parent
  thread_create()

scheduler
  child 먼저 실행

child
  initd()
  process_exec()
  exit()
  process_exit()
  curr->child_status가 아직 NULL일 수 있음
```

그래서 `child_status` record는 `thread_create()` 전에 만들어져 있어야 한다. 문제는 `thread_create()` 전에는 tid를 모른다는 점이다.

이 문제는 `tid`를 record 생성 시점에 반드시 알아야 한다는 생각을 버리면 풀린다.

```text
child_status_create()
  cs->tid = TID_ERROR 임시값

thread_create() 성공
  cs->tid = tid 로 갱신
```

child가 exit할 때는 `cs->tid`로 record를 찾지 않는다. child는 자기 `thread_current()->child_status` pointer로 record를 직접 접근한다. `cs->tid`는 parent가 나중에 `wait(child_tid)`에서 children list를 검색할 때 필요하다.

따라서 현재 helper는 `tid_t` 인자를 받지 않는다.

```c
static struct child_status *
child_status_create (void) {
	struct child_status *cs = malloc(sizeof *cs);
	if (cs == NULL) {
		return NULL;
	}

	cs->tid = TID_ERROR;
	...
	return cs;
}
```

## 22. `initd_aux`는 왜 필요한가

기존 `process_create_initd()`는 child에게 `fn_copy` 하나만 넘겼다.

```c
thread_create (thread_name, PRI_DEFAULT, initd, fn_copy);
```

그래서 기존 `initd()`는 인자를 command line page로 바로 해석했다.

```c
static void
initd (void *f_name)
{
	process_exec (f_name);
}
```

하지만 parent-child record를 연결하려면 child가 시작할 때 두 가지를 알아야 한다.

```text
fn_copy
  실행할 command line page

cs
  parent와 공유할 child status record
```

`thread_create()`의 aux 인자는 하나뿐이다. 그래서 둘을 한 포인터로 넘기기 위해 작은 상자 구조체를 만든다.

```c
struct initd_aux {
	char *fn_copy;
	struct child_status *cs;
};
```

이제 `thread_create()`에는 `fn_copy`가 아니라 `initd_aux`의 주소를 넘긴다.

```c
thread_create (thread_name, PRI_DEFAULT, initd, ia);
```

즉 `aux`는 갑자기 나온 개념이 아니라, child에게 넘겨야 할 값이 하나에서 두 개로 늘어서 필요한 전달 상자다.

## 23. `void *aux`로 받으면 `cs`를 어떻게 꺼내는가

`void *`는 "아직 타입을 모르는 포인터"다. 함수 선언은 `void *`로 유지하되, 함수 안에서 실제 타입으로 해석하면 된다.

헷갈렸던 부분은 인자 이름이 `f_name`이어서, `void *`로 받으면 여전히 command line만 들어오는 것처럼 보였다는 점이다. 지금은 `aux` 안에 `struct initd_aux *`가 들어온다.

현재 흐름은 다음과 같다.

```c
static void
initd (void *aux)
{
	struct initd_aux *ia = aux;
	char *fn_copy = ia->fn_copy;
	struct child_status *cs = ia->cs;

	thread_current ()->child_status = cs;
	process_init();
	free(ia);

	if (process_exec (fn_copy) < 0)
		PANIC ("Fail to launch initd\n");
	NOT_REACHED ();
}
```

`free(ia)` 전에 `fn_copy`와 `cs`를 지역 변수로 꺼내 둔다. 여기서 free하는 것은 aux 상자뿐이다.

```text
free(ia)
  initd_aux 상자만 해제

fn_copy
  process_exec(fn_copy)가 나중에 해제

cs
  parent/child가 공유하는 record이므로 여기서 해제하면 안 됨
```

## 24. `initd_aux *ia;`만 선언하면 왜 안 되는가

처음에는 다음처럼 포인터 변수만 선언하고 바로 필드를 채우려 했다.

```c
struct initd_aux *ia;
ia->fn_copy = fn_copy;
```

이 코드는 `ia`가 실제 상자를 가리키지 않기 때문에 잘못됐다. 포인터 변수는 주소를 담는 변수일 뿐이고, 그 주소에 실제 `struct initd_aux` 객체가 있어야 `ia->fn_copy`를 쓸 수 있다.

현재는 `malloc(sizeof *ia)`로 aux 상자 메모리를 만든다.

```c
struct initd_aux *ia = malloc(sizeof *ia);
if (ia == NULL) {
	palloc_free_page(fn_copy);
	return TID_ERROR;
}
ia->fn_copy = fn_copy;
```

이 실수는 `struct child_status *cs = malloc(sizeof *cs)`와 같은 종류다. 포인터를 선언하는 것과, 그 포인터가 가리킬 실제 객체를 만드는 것은 다르다.

## 25. 실패 경로에서 `free(cs)`와 `child_status_release(cs)` 중 무엇을 쓰는가

`thread_create()` 실패 경로에서는 `free(cs)`가 더 자연스럽다.

```c
tid = thread_create (thread_name, PRI_DEFAULT, initd, ia);
if (tid == TID_ERROR) {
	palloc_free_page (fn_copy);
	free(cs);
	free(ia);
	return TID_ERROR;
}
```

이 시점에는 child thread가 만들어지지 않았고, parent의 `children` list에도 record가 들어가지 않았다. 즉 parent/child가 record를 공유하는 수명 정책이 아직 시작되지 않았다.

`child_status_release(cs)`는 "parent나 child가 자기 몫 참조 하나를 내려놓는다"는 의미다.

```text
child_status_release(cs)
  ref_cnt--
  ref_cnt == 0이면 free
```

그런데 생성 직후 `ref_cnt`는 2다. 여기서 release를 한 번만 호출하면 1이 되어 free되지 않는다. 두 번 호출하면 free되기는 하지만, 실제 parent/child 참조가 시작되지도 않았는데 두 몫을 내려놓는 표현이라 의미가 어색하다.

정리하면 다음과 같다.

```text
thread_create() 전 또는 thread_create() 실패 경로
  아직 공유 record가 아님
  free(cs)

thread_create() 성공 후 parent/child가 공유하기 시작한 뒤
  child_status_release(cs)
```

`cs == NULL` 실패 경로에서는 `cs` 자체가 없으므로 `cs`를 free하지 않는다. 이미 만든 `fn_copy`, `ia`만 정리한다.

```c
struct child_status *cs = child_status_create ();
if (cs == NULL) {
	palloc_free_page (fn_copy);
	free(ia);
	return TID_ERROR;
}
```

## 26. `process_create_initd()` 현재 완료 상태

현재 `process_create_initd()` 연결에서 완료된 흐름은 다음이다.

```text
parent
  fn_copy 할당
  initd_aux 할당
  child_status record 할당
  thread_create(..., initd, ia)
  성공 후 cs->tid = tid
  parent->children에 cs 등록
  tid 반환

child
  initd(aux)
  aux에서 fn_copy, cs를 꺼냄
  thread_current()->child_status = cs
  aux 상자 free
  process_exec(fn_copy)
```

이 구조 때문에 child가 parent보다 먼저 실행되어도 child는 이미 aux를 통해 `cs`를 받을 수 있다.

이후 `process_exit()`에서 child가 status를 기록하고, `process_wait()`에서 parent가 status를 회수하는 흐름까지 연결했다. 같은 child status record 구조를 `process_fork()`에도 연결해야 한다.

## 27. `curr->child_status = NULL`을 `child_status_release()` 안에 넣으면 안 되는 이유

처음에는 `child_status_release(cs)` 안에서 `curr->child_status = NULL`까지 처리하면 편해 보일 수 있다. 하지만 그렇게 하면 helper의 책임이 섞인다.

`child_status_release(cs)`의 의미는 다음 하나다.

```text
인자로 받은 child status record의 참조 하나를 내려놓는다.
ref_cnt--
ref_cnt == 0이면 free
```

반면 `curr->child_status = NULL`은 현재 thread가 자기 parent에게 보고할 record pointer를 끊는 작업이다. 이 작업은 "지금 release하는 cs가 현재 thread 자신의 child_status record"라는 사실을 알고 있는 호출부에서만 해야 한다.

문제가 되는 이유는 `child_status_release()`가 child만 호출하는 함수가 아니기 때문이다.

```text
child process_exit()
  자기 parent에게 보고할 record를 release

parent process_wait()
  기다리던 child record를 release

parent process_exit()
  wait하지 않은 children record들을 release
```

예를 들어 parent가 `process_wait()`에서 어떤 child record를 release하는 중이라고 하자.

```text
cs
  parent가 기다리던 child의 record

thread_current()->child_status
  parent 자신이 자기 parent에게 보고할 record
```

이 둘은 다른 record일 수 있다. 그런데 release helper 내부에서 무조건 `thread_current()->child_status = NULL`을 해버리면, parent 자신의 보고용 record가 잘못 끊길 수 있다.

따라서 현재 구조는 다음처럼 나눈다.

```text
child_status_release(cs)
  ref_cnt만 감소
  필요하면 free

process_exit()
  이 cs가 현재 thread 자신의 child_status라는 것을 알고 있으므로
  release 후 curr->child_status = NULL
```

## 28. `list_pop_front()`는 `child_status *`를 반환하지 않는다

parent가 종료될 때 wait하지 않은 children record를 정리하려면 `curr->children` list를 비워야 한다.

처음 구현에서 헷갈릴 수 있는 부분은 `list_pop_front()`의 반환 타입이다.

```c
struct child_status *cs = list_pop_front (&curr->children);
```

이 흐름은 잘못됐다. PintOS list에 들어 있는 것은 `struct child_status` 자체가 아니라 record 안의 `struct list_elem elem`이다. 따라서 `list_pop_front()`는 `struct list_elem *`를 반환한다.

올바른 흐름은 다음이다.

```c
struct list_elem *e = list_pop_front (&curr->children);
struct child_status *cs = list_entry (e, struct child_status, elem);
child_status_release (cs);
```

즉 list에서 꺼낸 `list_elem`을 `list_entry()`로 감싸서 원래 `struct child_status *`를 복원해야 한다.

주의할 점은 `list_entry()`의 두 번째 인자도 실제 타입 이름이어야 한다는 것이다.

```c
list_entry (e, struct child_status, elem)
```

`child_status`라는 typedef를 만든 적이 없다면 `struct`를 빼면 안 된다.

## 29. `process_wait()`은 왜 child 종료 상태를 반환하는가

`process_wait(child_tid)`는 parent 자신의 종료 상태를 정하는 함수가 아니다. parent가 만든 child가 어떤 결과로 끝났는지 받아 오는 함수다.

child를 만든다는 것은 보통 parent가 어떤 일을 child에게 맡긴다는 뜻이다.

```text
parent
  child 생성
  child에게 일 맡김

child
  일을 수행
  exit(status)로 결과를 남김

parent
  wait(child)
  child가 남긴 status를 받아 다음 행동 결정
```

예를 들어 shell이 compiler를 실행하는 상황을 생각하면 쉽다.

```text
shell(parent)
  gcc 실행(child)

gcc(child)
  컴파일 성공 -> exit(0)
  컴파일 실패 -> exit(1)

shell(parent)
  wait(gcc)
  반환된 status가 0이면 다음 명령 진행
  1이면 실패 처리
```

그래서 child가 `exit(57)`을 호출하면, child의 종료 경로는 parent와 공유하는 child status record에 그 값을 남긴다.

```text
child process_exit()
  cs->exit_status = 57
  cs->has_exited = true
```

parent의 `process_wait()`은 child가 아직 살아 있으면 기다리고, child가 종료했으면 그 값을 반환한다.

```text
parent process_wait(child_tid)
  child가 살아 있으면 기다림
  child가 남긴 cs->exit_status 읽음
  record 정리
  child status 반환
```

여기서 헷갈리면 안 되는 구분은 다음이다.

```text
thread_current()->exit_status
  현재 parent process 자신이 나중에 종료할 때 남길 status

cs->exit_status
  child process가 종료하면서 parent에게 남긴 status

process_wait() 반환값
  cs->exit_status
```

따라서 `process_wait()`에서 child의 종료 상태를 parent의 `exit_status`에 덮어쓰면 의미가 섞인다. child status는 지역 변수에 저장해서 반환하고, parent 자신의 `exit_status`는 건드리지 않는 편이 맞다.

`wait()`이 "수거"라고 불리는 이유는 두 가지 일을 같이 하기 때문이다.

```text
1. child가 남긴 종료 결과를 회수한다.
2. parent가 들고 있던 child status record 참조를 정리한다.
```

즉 `process_wait()`은 기다리는 함수이면서, child의 종료 결과와 record를 회수하는 함수다.

## 30. ROX에서 실행 파일을 왜 닫지 않고 들고 있어야 하는가

ROX는 실행 중인 executable file에 write를 막는 기능이다.

처음에는 `load()`에서 실행 파일을 다 읽었으니 바로 닫아도 된다고 생각하기 쉽다.

```text
filesys_open(file_name)
ELF header 읽음
segment load
file_close(file)
```

하지만 이렇게 닫아 버리면 process가 살아 있는 동안 "이 파일은 실행 중이므로 write 금지"라는 상태를 유지할 file object가 사라진다.

ROX에서는 성공적으로 load한 file에 write deny를 걸고, 현재 thread가 그 file pointer를 들고 있어야 한다.

```text
load 성공
  file_deny_write(file)
  thread_current()->running_file = file
  file_close(file)는 하지 않음

process cleanup
  file_close(running_file)
  running_file = NULL
```

즉 성공 경로와 실패 경로가 달라진다.

```text
load 실패
  실행하지 않을 파일이므로 file_close(file)

load 성공
  실행 중인 파일이므로 close하지 않고 running_file에 보관
```

`thread.h`에는 `struct file *running_file`만 저장하므로 `struct file;` forward declaration이면 충분하다. `struct file`의 내부 필드를 직접 보지 않고 포인터만 저장하기 때문이다.

## 31. `file_close()`는 `file_allow_write()`까지 호출한다

현재 PintOS의 `file_close()` 구현은 내부에서 `file_allow_write(file)`를 호출한다.

```c
void
file_close (struct file *file) {
	if (file != NULL) {
		file_allow_write (file);
		inode_close (file->inode);
		free (file);
	}
}
```

따라서 cleanup에서 다음처럼 쓰면 된다.

```c
if (curr->running_file != NULL) {
	file_close (curr->running_file);
	curr->running_file = NULL;
}
```

`file_allow_write(curr->running_file)`를 명시적으로 먼저 호출해도 큰 문제는 없지만 중복이다. `file_allow_write()`는 이미 허용 상태이면 별일을 하지 않도록 되어 있지만, 현재 구현에서는 `file_close()`에 맡기는 쪽이 더 단순하다.

실패 경로에서도 `file_close(file)` 전에 `file != NULL` 확인이 필요하다. `filesys_open()`이 실패하면 file은 NULL일 수 있기 때문이다.

```c
if (file != NULL) {
	file_close (file);
}
```

## 32. 현재 단계에서 기억할 것

현재 구현은 `exit` 기본 구현, `process_create_initd()`의 child status 연결, `process_exit()`의 child status 기록과 children 정리, `process_wait()`의 status 회수, ROX 기본 처리까지 진행된 상태이고, 3번 담당 전체 구현이 끝난 것은 아니다.

지금 이해해야 할 핵심은 다음이다.

- user program의 `exit(status)`는 wrapper를 거쳐 `syscall_handler()`로 들어온다.
- `SYS_EXIT`는 status를 저장하고 종료 경로로 들어간다.
- `process_exit()`은 종료 메시지와 정리 작업을 담당한다.
- `exit_status`는 나중에 parent-child status 구조로 넘겨 줄 값이다.
- child status record는 `fork()`뿐 아니라 `process_create_initd()`로 만든 첫 user process에도 필요하다.
- `wait_sema`는 parent를 재우고 child exit 시 깨우는 event 동기화 장치다.
- `child_status.elem`은 parent의 `children` list용이고, semaphore waiters용이 아니다.
- `child_status_create()`, `child_status_find()`, `child_status_release()`의 기본 흐름은 구현했다.
- `process_create_initd()`는 child status record를 만들고 `initd(aux)`로 child에게 넘기는 흐름까지 연결했다.
- `process_exit()`은 child로서 status를 기록하고, parent로서 wait하지 않은 children record를 release한다.
- `process_wait()`은 child의 종료 상태를 반환해야 하며, parent 자신의 `exit_status`를 덮어쓰면 안 된다.
- `process_wait()`은 wait한 child record를 parent의 `children` list에서 제거하고 parent 몫 참조를 release한다.
- ROX에서는 load 성공 시 실행 파일을 닫지 않고 `running_file`에 보관해야 write deny가 유지된다.
- 다음은 이 helper들을 `process_fork()`에 연결할 차례다.
- `fork`, `exec` syscall 경로 점검이 들어오면 이 문서에 이어서 기록한다.
