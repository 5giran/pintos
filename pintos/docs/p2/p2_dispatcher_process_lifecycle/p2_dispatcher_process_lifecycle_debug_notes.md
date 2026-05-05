# PintOS Project 2 - 3번 담당 Debug Notes

## 배경

이 문서는 3번 담당 범위인 `Dispatcher / Fork / Exec / Exit / Rox`를 구현하면서 헷갈렸던 지점을 누적해서 정리한다.

현재는 `exit` 기본 구현과 parent-child status record 설계 초안에서 나온 질문을 정리한다. 이후 `wait`, `fork`, `exec`, `rox` 구현 중 나온 질문은 이 문서에 section을 추가한다.

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
  process.c에서 별도 구조체로 관리
```

즉 `struct thread`에는 record를 찾기 위한 list와 pointer만 두고, record 자체는 parent thread와 child thread의 수명에 묶이지 않도록 별도 kernel memory로 관리한다.

## 9. `waited`는 현재 wait 중이라는 뜻인가

`waited` 또는 현재 코드 초안의 `is_parent_wait`는 "현재 wait 중인가"만 뜻하지 않는다.

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

따라서 `is_parent_wait == true`는 두 상태를 모두 포함한다.

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
  child_status->is_child_exit == false
  sema_down(&child_status->wait_sema)

child
  child_status->exit_status = status
  child_status->is_child_exit = true
  sema_up(&child_status->wait_sema)
```

그래도 공유 자료가 없는 것은 아니다. parent와 child는 같은 `child_status`를 본다.

```text
child가 쓰는 값
  exit_status
  is_child_exit

parent가 읽고 쓰는 값
  exit_status
  is_child_exit
  is_parent_wait
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

## 14. 현재 단계에서 기억할 것

현재 구현은 `exit` 기본 구현일 뿐이고, 3번 담당 전체 구현이 끝난 것이 아니다.

지금 이해해야 할 핵심은 다음이다.

- user program의 `exit(status)`는 wrapper를 거쳐 `syscall_handler()`로 들어온다.
- `SYS_EXIT`는 status를 저장하고 종료 경로로 들어간다.
- `process_exit()`은 종료 메시지와 정리 작업을 담당한다.
- `exit_status`는 나중에 parent-child status 구조로 넘겨 줄 값이다.
- child status record는 `fork()`뿐 아니라 `process_create_initd()`로 만든 첫 user process에도 필요하다.
- `wait_sema`는 parent를 재우고 child exit 시 깨우는 event 동기화 장치다.
- `child_status.elem`은 parent의 `children` list용이고, semaphore waiters용이 아니다.
- `wait`, `fork`, `exec`, `rox` 구현이 들어오면 이 문서에 이어서 기록한다.
