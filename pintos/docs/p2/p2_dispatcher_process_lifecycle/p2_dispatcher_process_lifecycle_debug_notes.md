# PintOS Project 2 - 3번 담당 Debug Notes

## 배경

이 문서는 3번 담당 범위인 `Dispatcher / Fork / Exec / Exit / Rox`를 구현하면서 헷갈렸던 지점을 누적해서 정리한다.

현재는 `exit` 기본 구현과 관련된 질문만 정리한다. 이후 `wait`, `fork`, `exec`, `rox` 구현 중 나온 질문은 이 문서에 section을 추가한다.

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

## 7. 현재 단계에서 기억할 것

현재 구현은 `exit` 기본 구현일 뿐이고, 3번 담당 전체 구현이 끝난 것이 아니다.

지금 이해해야 할 핵심은 다음이다.

- user program의 `exit(status)`는 wrapper를 거쳐 `syscall_handler()`로 들어온다.
- `SYS_EXIT`는 status를 저장하고 종료 경로로 들어간다.
- `process_exit()`은 종료 메시지와 정리 작업을 담당한다.
- `exit_status`는 나중에 parent-child status 구조로 넘겨 줄 값이다.
- `wait`, `fork`, `exec`, `rox` 구현이 들어오면 이 문서에 이어서 기록한다.
