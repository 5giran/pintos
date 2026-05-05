# PintOS Project 2 - 3번 담당 Process Lifecycle 맥락 정리

## 배경

이 문서는 `p2_user_memory_syscall_parallel_work_guide.md`의 3번 담당 범위인 `Dispatcher / Fork / Exec / Exit / Rox`를 구현하기 전에 큰 그림을 잡기 위한 문서다.

목표는 의사코드나 정답 코드를 제공하는 것이 아니다. 대신 구현 전에 다음 감각을 잡는 것이다.

- 내가 맡은 기능들이 왜 한 묶음인지
- `fork`, `exec`, `wait`, `exit`이 서로 어떻게 연결되는지
- 어떤 상태를 저장해야 하는지
- 1번/2번 담당 작업과 내 작업이 어디서 만나는지

## 1. 3번 담당의 핵심은 프로세스 생명주기다

3번 담당이 맡은 syscall은 `halt`, `exit`, `fork`, `exec`, `wait`이다.

`halt`는 PintOS 전체를 끄는 단순 syscall이다. 하지만 `exit`, `fork`, `exec`, `wait`은 모두 사용자 프로세스의 생명주기와 관련된다.

`fork`는 현재 프로세스를 복제해서 자식 프로세스를 만든다. `exec`는 현재 프로세스가 실행 중인 프로그램을 다른 프로그램으로 바꾼다. `exit`은 현재 프로세스를 종료한다. `wait`은 부모가 자식의 종료를 기다리고 종료 status를 받아 오는 일이다.

그래서 3번 담당은 단순히 `switch` 문에 syscall case를 추가하는 사람이 아니다. 프로세스가 만들어지고, 복제되고, 다른 프로그램으로 바뀌고, 종료되고, 부모에게 결과를 전달하는 흐름을 완성하는 사람이다.

## 2. Project 2에서 process는 thread 위에 올라간다

PintOS Project 2에서는 user process 하나를 이렇게 보면 된다.

```text
user process 하나
  = kernel thread 하나
  + user address space 하나
  + fd table
  + parent-child 관계 정보
```

사용자 프로그램은 처음부터 user mode에서 시작하지 않는다. 새 kernel thread가 먼저 만들어지고, 그 thread가 kernel mode에서 `initd()`, `process_exec()`, `load()`를 실행한다. 준비가 끝나면 `do_iret()`을 통해 user mode로 내려가 사용자 프로그램을 실행한다.

사용자 프로그램이 syscall을 호출하면 다시 kernel mode로 들어온다. 이때 진입점이 `syscall_handler()`다.

이 구조 때문에 `process_fork()`, `process_exec()`, `process_wait()`, `process_exit()`은 thread와 process 개념을 함께 다룬다. Project 2에서는 "user process 하나는 kernel thread 하나 위에서 실행된다"라고 생각하면 이해가 쉽다.

## 3. Dispatcher는 syscall의 입구다

사용자 프로그램은 `exit(0)`, `fork("child")`, `exec("child-simple")`처럼 C 함수를 호출하는 것처럼 보인다. 하지만 실제로 커널에 들어올 때는 함수 이름이 전달되지 않는다.

커널은 CPU register를 보고 syscall을 구분한다.

```text
rax = syscall number
rdi = 1번째 인자
rsi = 2번째 인자
rdx = 3번째 인자
r10 = 4번째 인자
r8  = 5번째 인자
r9  = 6번째 인자
```

`syscall_handler()`는 `rax`를 보고 어떤 syscall인지 판단하고, 나머지 register에서 인자를 꺼낸다. 반환값이 필요한 syscall은 결과를 다시 `rax`에 넣어야 한다.

예를 들어 parent에서 `fork()`는 child pid를 반환해야 하고, child에서 `fork()`는 0을 반환해야 한다. `wait()`은 child의 exit status를 반환해야 한다. 이런 반환값은 모두 `f->R.rax`로 돌아간다.

## 4. exit은 종료 status를 남기는 일이다

사용자 프로그램은 끝날 때 status를 남긴다.

```c
exit (0);
exit (-1);
exit (42);
```

이 status는 두 곳에서 중요하다.

첫째, 테스트 output에 필요하다.

```text
args-single: exit(0)
child-bad: exit(-1)
```

둘째, 부모의 `wait(pid)` 반환값이 된다. 자식이 `exit(42)`로 종료했다면 부모의 `wait(child_pid)`는 42를 받아야 한다.

따라서 `exit(status)`는 단순히 현재 thread를 죽이는 것만으로 끝나면 안 된다. status를 저장하고, 종료 메시지를 정확히 한 번 출력하고, 들고 있던 자원을 정리하고, 부모가 기다리고 있으면 깨워야 한다.

주의할 점은 exit message가 정확히 한 번만 나와야 한다는 것이다. `SYS_EXIT`에서도 출력하고 `process_exit()`에서도 출력하면 두 번 나온다. 반대로 아무 데서도 출력하지 않으면 expected output이 맞지 않는다. 구현 전에 출력 위치를 하나로 정해야 한다.

## 5. process_exit은 뒷정리 장소다

`exit(status)`가 "이 status로 죽겠다"라는 요청이라면, `process_exit()`은 실제 정리 장소다.

프로세스가 들고 있는 대표 자원은 다음이다.

- user address space
- fd table
- 실행 중인 executable file
- parent-child 상태 정보

skeleton에는 이미 address space를 정리하는 `process_cleanup()`이 있다. Project 2에서는 여기에 fd 정리, 실행 파일 정리, wait 상태 정리가 붙는다.

책임을 나누면 이렇게 생각하면 된다.

```text
exit(status)
  종료 status를 정한다.

process_exit()
  프로세스가 들고 있던 자원을 정리한다.

thread_exit()
  scheduler 관점에서 현재 thread를 끝낸다.
```

이 셋의 책임을 섞으면 exit message 중복, fd leak, wait 실패가 생기기 쉽다.

## 6. wait은 자식의 결과표를 받는 일이다

부모가 자식을 만들었다고 하자.

```c
pid_t pid = fork ("child");
int status = wait (pid);
```

부모가 알고 싶은 것은 단순히 "시간이 지났나?"가 아니다. 부모는 다음을 확인해야 한다.

- 이 pid가 내 직접 자식인가?
- 이미 wait한 자식은 아닌가?
- 아직 살아 있으면 언제 끝나는가?
- 이미 죽었으면 exit status가 무엇인가?

그래서 `wait(pid)`는 단순 sleep이 아니라, 자식의 종료 결과를 정확히 한 번 회수하는 기능이다.

중요한 규칙은 다음이다.

- 내 직접 자식이 아니면 `-1`
- 같은 자식을 두 번 wait하면 두 번째는 `-1`
- 자식이 정상 exit하면 그 status 반환
- 자식이 exception 등으로 죽으면 `-1`

또 `process_wait()`은 user-level `wait()` syscall만을 위한 함수가 아니다. `threads/init.c`의 `run_task()`도 첫 user program이 끝나기 전 PintOS가 꺼지지 않도록 `process_wait()`을 호출한다.

## 7. parent-child status는 자식의 영수증이다

자식이 종료되면 child thread 구조체는 사라질 수 있다. 그런데 부모는 자식이 죽은 뒤에 `wait(pid)`를 호출할 수도 있다.

따라서 exit status를 child의 `struct thread` 안에만 저장하면 위험하다. child thread가 사라진 뒤 부모가 읽을 곳이 없기 때문이다.

그래서 부모와 자식 사이에 공유되는 상태표가 필요하다. 이 상태표는 자식 프로세스의 영수증처럼 생각하면 된다.

영수증에는 보통 이런 정보가 들어간다.

- 자식 pid
- 자식 exit status
- 자식이 이미 종료했는지
- 부모가 이미 wait했는지
- 부모가 기다릴 동기화 객체
- fork 복제 성공 여부

자식이 먼저 죽어도 영수증은 남아 있어야 한다. 부모가 wait하면 영수증을 보고 status를 가져간다. 한 번 가져간 영수증은 다시 쓸 수 없다.

이 구조는 2번 담당이 아니라 3번 담당 범위다. fd table이 아니라 `fork`, `exit`, `wait`이 공유하는 process lifecycle 상태이기 때문이다.

## 8. fork는 현재 프로세스를 복제하는 일이다

`fork()`는 새 thread 하나를 만드는 것보다 더 큰 의미가 있다. 현재 실행 중인 프로세스의 상태를 자식에게 복제해야 한다.

부모와 자식은 `fork()` 호출 직후 같은 코드 위치에서 이어서 실행된다. 하지만 반환값이 다르다.

```text
parent에서 fork() 반환값
  child pid

child에서 fork() 반환값
  0
```

그래서 사용자 코드는 `if (pid == 0)`으로 parent와 child 흐름을 나눌 수 있다.

이 동작을 만들려면 child가 parent의 register 상태를 거의 그대로 복사해야 한다. 단, child가 보는 `fork()` 반환값만 0이어야 한다. 함수 반환값은 `rax` register에 들어가므로 child의 `rax`를 0으로 바꿔야 한다.

fork에서 복제해야 하는 것은 크게 네 가지다.

- 실행 중이던 register 상태
- user address space
- fd table
- parent-child 상태 정보

user address space는 parent가 보던 code, data, stack 같은 사용자 메모리 세계다. VM이 꺼진 Project 2에서는 parent의 page table을 돌면서 user page를 child용 새 page에 복사하는 방식이 필요하다.

fd table도 복제해야 한다. 부모가 열어 둔 파일을 자식도 같은 fd 번호로 사용할 수 있어야 하기 때문이다. 이때 fd table 내부 복제는 2번 담당 API를 사용하더라도, fork 전체 흐름은 3번 담당이 소유한다.

## 9. fork에서 parent가 기다려야 하는 이유

parent는 `fork()`가 성공했는지 알아야 한다. 문제는 child 생성이 중간에 실패할 수 있다는 점이다.

예를 들어 child page table 생성, user page 복제, fd table 복제 중 하나가 실패할 수 있다.

parent가 child thread를 만들자마자 바로 성공으로 반환하면, 실제로 child 준비가 실패했는데도 parent는 성공했다고 믿고 진행한다. 그래서 parent는 child가 복제 성공/실패를 기록할 때까지 기다려야 한다.

흐름은 이렇게 이해하면 된다.

```text
parent
  child 생성 요청
  child 준비 완료 신호를 기다림
  성공이면 child pid 반환
  실패이면 -1 반환

child
  address space 복제
  fd table 복제
  성공/실패 기록
  parent 깨움
  성공이면 user mode로 진입
```

이 대기 구조가 없으면 간단한 fork 테스트는 우연히 통과할 수 있어도, 여러 child를 만들거나 메모리가 부족한 상황에서 깨지기 쉽다.

## 10. exec는 현재 프로세스의 프로그램만 바꾸는 일이다

`exec()`는 새 process를 만드는 syscall이 아니다. 현재 프로세스가 실행 중인 프로그램 이미지를 다른 프로그램으로 바꾸는 syscall이다.

중요한 점은 다음이다.

- 현재 thread는 유지된다.
- fd table도 유지된다.
- thread name도 바뀌지 않는다.
- 기존 user address space는 버린다.
- 새 executable을 load한다.
- 새 user stack을 만든다.
- 성공하면 기존 코드 위치로 돌아오지 않는다.

성공한 `exec()`는 반환하지 않는다. 새 프로그램으로 진입하기 때문이다.

반대로 실패하면 현재 프로세스는 `exit(-1)`로 종료되어야 한다. 없는 파일을 exec하거나 잘못된 포인터를 넘긴 경우가 여기에 해당한다.

## 11. exec에서 cmd_line을 먼저 안전하게 확보해야 한다

`exec(cmd_line)`의 `cmd_line`은 user pointer다. 정상 문자열일 수도 있고, 잘못된 주소일 수도 있다.

또 `process_exec()`는 기존 address space를 정리할 수 있다. 기존 address space가 사라지면, 그 안에 있던 user pointer는 더 이상 안전하게 읽을 수 없다.

그래서 exec syscall은 먼저 user cmdline을 안전한 kernel 쪽 저장소로 가져와야 한다. 그 다음 그 kernel cmdline을 `process_exec()`에 넘겨야 한다.

이 부분은 1번 담당의 user memory 기능과 연결된다.

## 12. rox는 실행 중인 파일을 덮어쓰지 못하게 하는 일이다

rox는 read-only executable 계열 테스트를 말한다. 실행 중인 프로그램 파일에 write가 성공하면 안 된다.

상황은 이렇다.

```text
프로세스가 어떤 executable을 실행 중이다.
그 프로세스나 다른 프로세스가 같은 executable 파일을 open한다.
그 파일에 write를 시도한다.
```

운영체제는 실행 중인 프로그램 파일이 덮어써지는 것을 막아야 한다.

PintOS에는 `file_deny_write(file)`이 있다. 중요한 점은 deny를 건 file을 바로 닫으면 안 된다는 것이다. 파일을 닫으면 deny가 풀릴 수 있다.

그래서 실행 파일을 load한 뒤에는 다음 흐름이 필요하다.

```text
executable open
file_deny_write(file)
running_file에 저장
process_exit 또는 다음 exec 때 close
```

이 때문에 `running_file` 같은 필드가 필요하다.

## 13. 1번 담당과 만나는 지점

1번 담당은 user pointer safety를 맡는다.

3번 담당이 user pointer를 직접 받는 대표 syscall은 다음이다.

```text
fork(thread_name)
exec(cmd_line)
```

`thread_name`, `cmd_line`은 user string이다. 바로 커널 로직에 넘기면 안 된다.

3번 담당 관점에서는 이렇게 이해하면 된다.

```text
user pointer가 들어온다.
  -> 1번 담당 API로 안전한 문자열인지 확인하거나 kernel 쪽에 확보한다.
  -> 그 다음 fork 또는 exec 로직에 사용한다.
```

즉 3번 담당은 user pointer 검증 내부 구현을 깊게 알 필요는 없지만, 언제 1번 담당 API를 거쳐야 하는지는 알아야 한다.

## 14. 2번 담당과 만나는 지점

2번 담당은 fd table과 파일 syscall을 맡는다.

3번 담당이 fd table과 만나는 지점은 다음이다.

- process 시작 시 fd table 초기화
- fork 시 parent fd table을 child에게 복제
- exec 시 fd table 유지
- exit 시 열린 fd 모두 close

정리하면 다음이다.

```text
parent-child status
  3번 담당

fd table 구조와 file syscall
  2번 담당

fork/exec/exit에서 fd table을 언제 초기화, 복제, 유지, 정리할지
  3번 담당이 흐름 소유
```

## 15. 구현 전에 먼저 정해야 할 상태들

3번 담당이 구현 전에 먼저 생각해야 하는 상태는 다음이다.

- 현재 process의 exit status
- 부모에게 내 종료를 알려 줄 상태표
- 내 자식 목록
- 자식이 이미 wait 되었는지
- 자식이 종료했는지
- fork 복제 성공 여부
- running_file

특히 중요한 것은 parent-child 상태다. 부모와 자식은 누가 먼저 죽을지 모른다.

자식이 먼저 죽고 부모가 나중에 wait할 수 있다. 부모가 먼저 wait으로 기다리고 있다가 자식이 나중에 죽을 수도 있다. 부모가 wait하지 않고 죽을 수도 있다. 부모가 같은 자식을 두 번 wait할 수도 있다.

이 모든 경우를 처리하려면 "자식 thread 자체"와 "부모가 볼 자식 상태표"를 구분해야 한다.

## 16. 테스트 이름을 기능으로 읽는 법

테스트 이름은 대체로 어떤 기능을 찌르는지 알려 준다.

- `exit`: exit message와 status 저장
- `wait-simple`: 부모가 자식 status를 받는 기본 흐름
- `wait-twice`: 같은 자식을 두 번 wait하면 실패해야 함
- `wait-bad-pid`: 내 직접 자식이 아닌 pid는 기다릴 수 없음
- `wait-killed`: 비정상 종료한 자식 status는 -1
- `fork-once`: parent와 child 반환값 분기
- `fork-multiple`: 여러 자식 생성과 wait
- `fork-read`, `fork-close`: fork 후 fd table 복제와 독립성
- `exec-once`: 현재 process image 교체
- `exec-arg`: exec 후 argument passing
- `exec-missing`: 없는 파일 exec 실패 시 exit(-1)
- `exec-read`: exec 후에도 fd table 유지
- `rox-simple`, `rox-child`, `rox-multichild`: 실행 중인 executable write deny 유지
- `multi-oom`: 반복 생성/실패 상황에서 자원 누수 확인

테스트를 전체로 한 번에 보면 막막하다. 하지만 위처럼 기능별로 보면 어떤 상태가 빠졌는지 추적하기 쉬워진다.

## 17. 한 장짜리 그림

3번 담당의 전체 흐름은 이렇게 이어진다.

```text
user program
  syscall 호출

syscall_handler
  syscall 번호 확인
  register에서 인자 추출
  필요한 경우 user pointer 검증
  fork/exec/wait/exit 로직 호출

fork
  child process를 만들고 parent-child 상태 연결

exec
  현재 process의 프로그램 이미지를 교체

exit
  status 저장, 자원 정리, parent에게 알림

wait
  parent가 child status를 정확히 한 번 회수

rox
  process가 살아 있는 동안 실행 파일 write deny 유지
```

이 기능들은 따로 떨어진 syscall이 아니다. 하나의 생명주기다.

```text
프로세스 생성 또는 fork
  -> 실행
  -> exec로 프로그램이 바뀔 수 있음
  -> exit로 종료
  -> parent가 wait으로 결과 수거
  -> fd와 running_file 등 자원 정리
```

## 18. 한 줄 정리

3번 담당이 구현하는 것은 "프로세스가 태어나고, 복제되고, 다른 프로그램으로 바뀌고, 종료되고, 부모에게 결과를 전달하는 흐름"이다.

```text
dispatcher는 입구
fork는 복제
exec는 교체
exit는 종료
wait은 결과 회수
rox는 실행 파일 보호
```

이렇게 보면 3번 담당 범위가 왜 한 묶음인지 보인다. 모두 process lifecycle의 다른 순간을 담당하기 때문이다.
