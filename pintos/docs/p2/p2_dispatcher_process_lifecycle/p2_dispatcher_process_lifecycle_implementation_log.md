# PintOS Project 2 - 3번 담당 구현 기록

## 1. 문서 목적

이 문서는 `p2_user_memory_syscall_parallel_work_guide.md`의 3번 담당 범위인 `Dispatcher / Fork / Exec / Exit / Rox` 구현 기록을 누적해서 정리하는 문서다.

구현 하나마다 문서를 새로 만들지 않고, 기능이 추가될 때마다 이 문서 안에 section을 추가한다. 현재는 `exit` 기본 구현까지만 기록한다.

## 2. Exit 기본 구현

### 2.1. 이전 구조

기존에는 `SYS_EXIT`가 `syscall.c` 안의 helper에서 바로 출력하고 종료했다.

```text
SYS_EXIT
  exit_with_status(status)

exit_with_status(status)
  exit message 출력
  thread_exit()

thread_exit()
  process_exit()
```

이 구조에서는 `process_exit()`이 종료 status를 알지 못했다. 따라서 나중에 `wait(pid)`가 자식의 종료 status를 받아 가려면 status를 저장할 별도 위치가 필요했다.

### 2.2. 현재 변경

현재 변경은 `exit(status)`를 "출력하고 끝내는 helper"가 아니라 "현재 process의 종료 status를 정하고 종료 경로로 들어가는 syscall"로 보는 방향이다.

변경 사항은 다음이다.

- `struct thread`에 `exit_status`를 추가했다.
- `init_thread()`에서 `exit_status` 기본값을 `-1`로 초기화했다.
- `SYS_EXIT`는 `f->R.rdi`에서 받은 status를 `thread_current()->exit_status`에 저장한 뒤 `thread_exit()`을 호출한다.
- `process_exit()`은 user process thread일 때만 exit message를 출력하고, 이후 `process_cleanup()`을 호출한다.

현재 흐름은 다음과 같다.

```text
user program
  exit(57)

syscall_handler()
  SYS_EXIT
  current->exit_status = 57
  thread_exit()

thread_exit()
  process_exit()

process_exit()
  exit message 출력
  process_cleanup()
```

### 2.3. 설계 의도

종료 메시지를 `process_exit()`로 옮기는 이유는 종료 처리의 공통 지점이기 때문이다.

앞으로 `process_exit()`에는 다음 작업들이 더 붙는다.

- parent-child 상태에 exit status 기록
- 부모가 기다리고 있으면 깨우기
- fd table 정리
- 실행 파일 write deny 해제
- address space 정리

따라서 `SYS_EXIT`는 status를 정하는 곳, `process_exit()`은 실제 종료 정리를 하는 곳으로 나누는 편이 이후 구현과 잘 맞는다.

`exit_status`는 최종 parent-child 구조를 대체하는 값은 아니다. 자식 thread는 종료 후 사라질 수 있으므로, 나중에는 이 값을 부모와 공유하는 child status 구조에 복사해야 한다. 현재는 그 값을 넘겨 주기 위한 첫 저장 위치다.

### 2.4. 현재 한계와 주의점

현재 구현은 `exit` 기본 구현 단계다. 아직 3번 담당 전체 구현이 끝난 것은 아니다.

남은 작업은 다음이다.

- parent-child status 구조 설계
- `process_wait()` 완성
- `fork` 성공/실패 동기화
- `exec` 인자 복사와 load 실패 처리 연결
- `process_exit()`에서 child status 기록과 부모 wake-up 처리
- `rox` 관련 실행 파일 write deny 처리

또 비정상 종료 경로에서는 최종적으로 `exit_status = -1`이 보장되어야 한다. 현재는 기본값을 `-1`로 초기화했기 때문에 `thread_exit()`만 호출해도 값이 유지되지만, 코드 의도를 명확히 하려면 invalid syscall이나 invalid pointer 경로에서 `exit_status = -1`을 명시하는 편이 좋다.

## 3. Parent-child status record 설계 맥락

`exit` 기본 구현으로 현재 process의 `exit_status`는 저장할 수 있게 되었다. 하지만 이 값이 `struct thread` 안에만 있으면 `wait()` 구현에는 부족하다.

여기서 말하는 parent-child 관계는 `fork()`로 생기는 관계만 뜻하지 않는다. PintOS가 첫 user process를 만들 때도 같은 문제가 생긴다.

```text
kernel main/init thread
  process_create_initd("args-single ...")
  initd user process thread 생성
  process_wait(initd_tid)
```

이 흐름에서는 parent가 user process가 아니라 PintOS 내부 kernel thread다. 하지만 "누군가 child thread를 만들고, 나중에 그 child의 종료를 기다린다"는 구조는 같다.

반면 `fork()`에서는 parent와 child가 모두 user process다.

```text
user process parent
  fork()
  user process child 생성
  wait(child_tid)
```

따라서 child status record는 `fork()` 전용 자료구조가 아니다. 더 정확히는 `process_create_initd()`나 `process_fork()`처럼 wait 가능한 child를 만드는 경로에서 필요한 자료구조다.

핵심 문제는 **부모와 자식의 실행 순서가 고정되어 있지 않다**는 점이다. 예를 들어 부모가 자식을 만든 직후, 스케줄러가 바로 자식을 먼저 실행할 수 있다.

```text
parent
  tid = fork("child") 호출

scheduler
  child를 먼저 실행

child
  exit(42)
  process_exit()
  thread_exit()

parent
  다시 실행됨
  wait(tid) 호출
```

이 흐름에서는 부모가 `wait(tid)`를 호출하는 시점에 자식은 이미 종료되어 있을 수 있다. 자식의 `struct thread`는 이미 종료 경로에 들어갔고, 이후 스케줄러에 의해 정리될 수 있으므로 **부모가 자식 thread 구조체를 직접 붙잡고 status를 읽는 방식은 위험**하다.

그래서 부모와 자식 사이에 별도의 `child status record`가 필요하다. 이 record는 자식 thread 자체가 아니라, **부모가 `wait()`으로 결과를 회수할 때까지 살아 있어야 하는 작은 상태표**다.

이 문제는 두 단계로 나누어 생각해야 한다.

```text
1단계
  record를 어디에 둘 것인가?

2단계
  별도로 둔 record를 언제 해제할 것인가?
```

먼저 1단계 문제는 "record를 parent나 child thread 안에 두면 왜 위험한가"이다.

```text
child thread 안에 record를 둔 경우
  child가 먼저 exit하면 child thread가 정리될 수 있다.
  parent가 나중에 wait할 때 읽을 record가 사라졌을 수 있다.

parent thread 안에 record를 둔 경우
  parent가 wait하지 않고 먼저 exit하면 parent thread가 정리될 수 있다.
  child가 나중에 exit status를 기록할 record가 사라졌을 수 있다.
```

따라서 record는 parent thread 안에도, child thread 안에도 통째로 넣지 않는다. record는 parent/child thread 바깥의 별도 kernel memory에 두고, parent와 child가 각각 pointer로 같은 record를 참조하게 만든다.

```text
parent thread
  children list로 child_status record를 가리킨다.

child thread
  child_status pointer로 자기 record를 가리킨다.

child_status record
  parent/child thread 바깥에 따로 존재한다.
```

그 다음 2단계 문제가 생긴다. record를 별도 구조체로 뺐다면, 이제 "언제 free할 것인가"를 정해야 한다.

```text
child가 exit할 때 바로 free하면
  parent가 나중에 wait할 수 없어진다.

parent가 exit할 때 바로 free하면
  child가 나중에 exit status를 기록할 수 없어진다.
```

그래서 별도 record에는 reference count나 orphan 처리 같은 수명 정책이 필요하다. 핵심은 parent와 child가 모두 더 이상 record를 쓰지 않을 때만 해제하는 것이다.

이 record가 맡아야 하는 책임은 다음이다.

- 이 record가 어떤 child tid에 대한 것인지 저장한다.
- child의 `exit_status`를 저장한다.
- child가 이미 종료되었는지 표시한다.
- parent가 이미 wait했는지 표시한다.
- child가 아직 살아 있으면 parent가 block할 수 있는 동기화 객체를 가진다.
- parent가 먼저 죽거나 child가 먼저 죽어도 dangling pointer가 생기지 않도록 수명 정책을 가진다.

즉 `thread.exit_status`는 "현재 자식이 죽을 때 남기는 값"이고, child status record는 "부모가 나중에 회수할 수 있도록 보관하는 값"이다.

이 설계가 정해져야 `process_create_initd()`, `fork`, `exit`, `wait`을 같은 구조 위에 연결할 수 있다.

```text
process_create_initd
  첫 user process용 child status record 생성
  kernel main/init thread의 child list에 등록

fork
  child status record 생성
  parent의 child list에 등록
  child thread가 자신의 record를 알 수 있게 연결

exit
  현재 exit_status를 child status record에 기록
  종료 완료 표시
  기다리는 parent가 있으면 깨움

wait
  parent의 child list에서 record 검색
  이미 wait했으면 -1
  child가 살아 있으면 기다림
  child가 죽었으면 저장된 status 반환
  record 정리
```

`exec`는 이 관계를 새로 만들지 않는다. `exec`는 현재 process의 프로그램 이미지를 다른 프로그램으로 교체하는 작업이다. 따라서 parent-child record를 새로 생성하지 않고, 기존 process의 관계와 fd table은 유지된다.

### 3.1. 현재 코드 초안

현재 코드에서는 `thread.h`에 parent-child 관계를 따라가기 위한 필드를 추가하기 시작했다.

```text
struct thread
  children
    현재 thread가 만든 direct child들의 child status record 목록

  child_status
    현재 thread가 자기 parent에게 보고할 때 사용할 자기 record
```

이 구조에서 중요한 점은 `child_status record 자체`를 `struct thread` 안에 통째로 넣지 않는다는 것이다. `struct thread`에는 record를 찾아가기 위한 list와 pointer만 둔다.

record 자체는 `process.c`에서 별도 구조체로 두는 방향이다.

```text
struct child_status
  tid
  exit_status
  is_child_exit
  is_parent_wait
  wait_sema
  elem
```

각 필드의 의미는 다음처럼 잡는다.

```text
tid
  어떤 child에 대한 record인지 식별한다.

exit_status
  child가 종료할 때 남긴 status다.

is_child_exit
  child가 이미 종료했는지 나타낸다.

is_parent_wait
  parent가 이 child에 대한 wait 권한을 이미 사용했는지 나타낸다.
  "현재 기다리는 중인가"만 뜻하는 것이 아니라,
  이미 회수했거나 회수 예정이라 다시 wait하면 안 되는 상태를 뜻한다.

wait_sema
  child가 아직 살아 있을 때 parent를 재워 두는 semaphore다.

elem
  parent->children list에 이 child_status record를 매달기 위한 list element다.
```

`elem`은 semaphore waiters에 들어가는 element가 아니다. `sema_down()`으로 parent가 잠들 때 semaphore waiters에 들어가는 것은 parent thread의 `thread.elem`이다. `child_status.elem`은 parent의 child record 목록에 들어가기 위한 별도 list element다.

### 3.2. `wait_sema`가 필요한 이유

`process_wait(child_tid)`를 호출했을 때 child가 이미 죽어 있으면 parent는 저장된 `exit_status`를 바로 반환하면 된다.

하지만 child가 아직 살아 있다면 parent는 status를 아직 읽을 수 없다. 이때 busy wait을 하면 CPU만 낭비하고 정확한 동기화도 되지 않는다.

busy wait이 아예 불가능한 것은 아니다. PintOS는 timer interrupt로 실행 중인 thread를 선점할 수 있으므로, parent가 `cnt++` loop를 도는 중에도 scheduler가 child를 실행시킬 수 있다. 그래서 "오래 도는 사이 child가 exit했기를 기대하는" 방식은 우연히 동작할 수 있다.

하지만 이것은 자식 종료를 정확히 기다리는 것이 아니다. child가 이미 죽었는데도 parent가 계속 loop를 돌 수 있고, 반대로 loop가 먼저 끝나 child가 아직 살아 있는데 parent가 return할 수도 있다.

그래서 parent는 child status record 안의 `wait_sema`에서 잠든다.

```text
parent
  wait(child_tid)
  is_child_exit == false
  sema_down(&child_status->wait_sema)

child
  exit(42)
  child_status->exit_status = 42
  child_status->is_child_exit = true
  sema_up(&child_status->wait_sema)

parent
  깨어남
  exit_status 반환
```

이 semaphore는 lock처럼 공유 자료 접근을 막는 용도라기보다, "child exit가 끝났으니 parent가 계속 진행해도 된다"는 event를 전달하는 용도에 가깝다.

### 3.3. 아직 남은 설계 지점

현재 코드 초안에는 record의 수명 정책이 아직 들어가지 않았다. record를 별도 구조체로 두기로 했다면, 다음 단계는 release 정책을 명확히 하는 것이다.

첫 user process 흐름만 보면 이 부분이 헷갈릴 수 있다. PintOS의 첫 user process는 kernel main/init thread가 만들고 `process_wait(initd_tid)`로 기다린다. 그래서 처음에는 "parent는 child를 항상 wait하는 것 아닌가?"라고 생각하기 쉽다.

하지만 일반적인 `fork()` 관계에서는 parent가 반드시 wait한다는 보장이 없다.

```text
parent
  fork()
  wait하지 않음
  exit(0)

child
  계속 실행
  나중에 exit(42)
```

또 parent가 child 여러 개를 만든 뒤 일부만 wait하고 종료할 수도 있다.

```text
parent
  child_a = fork()
  child_b = fork()
  wait(child_a)
  exit(0)

child_b
  parent보다 나중에 exit
```

따라서 "첫 user process의 parent는 기다린다"는 사실과 "모든 parent가 항상 모든 child를 기다린다"는 말은 다르다. `process_wait()`은 가능한 동작이지 강제 동작이 아니다.

`fork()` 후 parent가 바로 exit하는 패턴이 항상 흔하거나 바람직한 사용법은 아닐 수 있다. 어떤 상황에서는 현재 process를 새 프로그램으로 바꾸는 `exec()`가 더 적절하다. 하지만 `fork()`와 `exec()`는 의미가 다르다.

```text
fork
  새 child process를 만든다.
  parent와 child가 동시에 존재할 수 있다.
  parent가 wait할 수도 있고, 안 할 수도 있다.

exec
  현재 process의 프로그램 이미지를 교체한다.
  새 child를 만들지 않는다.
  parent-child record도 새로 생기지 않는다.
```

OS는 user program이 어떤 순서로 `fork`, `wait`, `exit`을 호출하더라도 kernel memory가 깨지지 않도록 방어해야 한다. 그래서 child status record를 parent thread나 child thread 내부가 아니라 별도 구조체로 두고, 수명 정책을 따로 가져가야 한다.

또 현재 초안 기준으로 확인해야 할 구현 사항은 다음이다.

- `thread.h`에서 `struct child_status *`를 쓰려면 forward declaration이 필요하다.
- `init_thread()`에서 `children`뿐 아니라 `child_status = NULL`도 초기화해야 한다.
- `struct child_status`를 만들 때 `wait_sema`는 0으로 초기화해야 한다.
- record 생성 helper, 검색 helper, release helper를 둘지 결정해야 한다.
- `process_create_initd()`와 `process_fork()` 양쪽에서 record를 생성해 parent의 `children`에 등록해야 한다.
- child thread가 시작할 때 자기 `child_status` pointer를 가져야 한다.
- `process_exit()`에서 자기 `child_status`에 exit status를 기록하고 parent를 깨워야 한다.
- `process_wait()`에서 parent의 `children` list를 검색하고, `is_parent_wait` 규칙을 적용해야 한다.

## 4. 이후 기록 위치

앞으로 구현이 추가되면 이 문서에 다음 section을 이어서 추가한다.

```text
4. process_wait()
5. process_fork()
6. process_exec()
7. Rox 처리
```

각 section은 "이전 구조", "현재 변경", "설계 의도", "남은 한계" 정도만 짧게 기록한다.
