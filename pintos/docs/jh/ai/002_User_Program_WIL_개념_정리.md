# User Program WIL 개념 정리

## 전체 흐름

이번 user program 주차에서 본 큰 흐름은 다음과 같다.

```text
커널 부팅
-> run_task()
-> process_create_initd()
-> thread_create()
-> initd()
-> process_exec()
-> load()
-> setup_argument_stack()
-> do_iret()
-> 유저 프로그램 실행
-> syscall 발생
-> syscall_handler()
-> dispatch_syscall()
-> 각 syscall handler 실행
```

핵심은 커널이 유저 프로그램을 직접 실행하는 것이 아니라, 실행할 프로그램을 로드하고 유저 스택과 레지스터를 준비한 뒤 `do_iret()`을 통해 유저 모드로 전환한다는 점이다.

## Process와 Thread

Pintos user program에서는 하나의 유저 프로세스가 하나의 커널 스레드 위에서 실행되는 구조로 이해할 수 있다.

- `thread_create()`는 새 커널 스레드를 만든다.
- `process_create_initd()`는 최초 유저 프로그램을 실행할 스레드를 만든다.
- `initd()`는 새 스레드에서 `process_exec()`를 호출한다.
- `process_exec()`는 현재 스레드의 실행 이미지를 유저 프로그램으로 교체한다.

여기서 `thread_current()`는 현재 CPU에서 실행 중인 커널 스레드를 의미한다. 부모/자식 관계를 다룰 때도 실제로는 `struct thread` 안에 프로세스 관련 정보를 함께 들고 있는 구조이다.

## Command Line과 Argument Passing

커맨드라인 예시는 다음처럼 들어온다.

```text
args-multiple some arguments for you!
```

이 문자열은 두 가지 용도로 나뉜다.

- 스레드 이름: 첫 번째 토큰인 `args-multiple`
- 유저 프로그램 인자: 전체 문자열을 파싱한 `argv[]`

`process_exec()` 안에서 커맨드라인을 공백 기준으로 나누고, 각 토큰을 유저 스택에 복사한다.

유저 프로그램의 `main(argc, argv)`로 전달하기 위해 중요한 값은 다음 두 개다.

- `rdi`: `argc`
- `rsi`: `argv` 시작 주소

문자열 자체와 `argv[i]` 포인터 배열은 유저 스택에 들어가지만, `argc`와 `argv`의 시작 주소는 x86-64 호출 규약에 따라 레지스터로 전달된다.

## User Stack

유저 스택은 유저 프로그램이 사용할 메모리 공간이다. argument passing에서는 커널이 유저 스택에 데이터를 미리 배치한다.

스택에는 보통 다음 순서의 데이터가 들어간다.

```text
문자열들
정렬 패딩
argv 포인터들
fake return address
```

스택은 높은 주소에서 낮은 주소 방향으로 자란다. 그래서 문자열과 포인터를 넣을 때 `rsp`를 감소시키면서 데이터를 복사한다.

정렬도 중요하다. 64비트 환경에서는 포인터 크기가 8바이트이므로, `rsp`를 8바이트 단위에 맞추는 작업이 필요하다.

## intr_frame과 do_iret

`struct intr_frame`은 유저 프로그램을 시작할 때 CPU 레지스터 상태를 담는 구조체다.

중요하게 본 필드는 다음과 같다.

- `rip`: 유저 프로그램이 시작할 명령어 주소
- `rsp`: 유저 스택 포인터
- `rdi`: 첫 번째 인자, 여기서는 `argc`
- `rsi`: 두 번째 인자, 여기서는 `argv`
- `cs`, `ss`, `ds`, `es`: 유저 모드 세그먼트 설정
- `eflags`: 인터럽트 플래그 등 CPU 상태

`do_iret()`은 이 `intr_frame`에 준비된 값을 CPU 레지스터로 복원하고, 커널 모드에서 유저 모드로 전환한다.

즉 `do_iret()`은 단순 함수 호출이 아니라 실행 권한과 실행 위치를 바꾸는 지점이다.

## load와 실행 파일

`load()`는 ELF 실행 파일을 열고, 실행 가능한 세그먼트를 유저 주소 공간에 적재한다.

흐름은 대략 다음과 같다.

```text
filesys_open()
-> ELF header 읽기
-> program header 검사
-> segment load
-> setup_stack()
-> entry point 설정
```

`load()`가 성공하면 `intr_frame.rip`에는 유저 프로그램의 시작 주소가 들어가고, `intr_frame.rsp`에는 초기 유저 스택 주소가 들어간다.

## System Call 흐름

유저 프로그램이 `write()`, `exit()`, `exec()` 같은 함수를 호출하면 직접 커널 함수를 부르는 것이 아니라 syscall 경로를 탄다.

```text
유저 프로그램
-> syscall instruction
-> syscall_entry
-> syscall_handler()
-> init_syscall_entry()
-> dispatch_syscall()
-> handle_write(), handle_exit() 등
```

`init_syscall_entry()`는 유저가 레지스터에 담아 보낸 syscall 번호와 인자들을 커널에서 쓰기 쉬운 구조체로 옮긴다.

x86-64 syscall 인자 매핑은 다음처럼 이해했다.

- `rax`: syscall 번호
- `rdi`: 1번 인자
- `rsi`: 2번 인자
- `rdx`: 3번 인자
- `r10`: 4번 인자
- `r8`: 5번 인자
- `r9`: 6번 인자

반환값은 다시 `rax`에 넣어 유저 프로그램으로 돌려준다.

## User Memory 검증

syscall에서 유저가 넘긴 포인터는 커널이 바로 믿으면 안 된다.

예를 들어 `write(fd, buffer, size)`에서 `buffer`는 유저 주소다. 이 주소가 다음 조건을 만족하는지 확인해야 한다.

- `NULL`이 아닌가
- 커널 주소가 아닌 유저 주소인가
- 현재 프로세스의 페이지 테이블에 실제로 매핑되어 있는가

관련 개념은 다음과 같다.

- `is_user_vaddr()`: 주소가 유저 영역인지 확인
- `pml4_get_page()`: 유저 가상 주소가 실제 물리 페이지에 매핑되어 있는지 확인
- `is_valid_user_string()`: 문자열 포인터 검증
- `is_valid_user_buffer()`: 버퍼 범위 검증

유저 메모리 검증은 `open`, `read`, `write`, `exec`, `create`, `remove` 같은 syscall에서 특히 중요하다.

## PML4와 주소 공간

PML4는 x86-64에서 사용하는 최상위 페이지 테이블이다.

프로세스마다 자기 유저 주소 공간을 가지고 있고, `thread_current()->pml4`는 현재 프로세스의 페이지 테이블을 가리킨다.

커널이 유저 포인터를 사용할 때는 단순히 주소값만 보는 것이 아니라, 현재 프로세스의 PML4를 기준으로 그 주소가 실제 접근 가능한 주소인지 확인해야 한다.

## exit와 wait

자식 프로세스의 종료 상태를 부모가 나중에 확인하려면 종료 상태를 저장해 둘 구조가 필요하다.

이 역할을 하는 것이 `child_status`다.

`child_status`에는 보통 다음 정보가 들어간다.

- `tid`: 자식 스레드 id
- `exit_status`: 자식의 종료 상태
- `waited`: 이미 wait 했는지 여부
- `exited`: 자식이 종료되었는지 여부
- `wait_sema`: 부모가 자식 종료를 기다리기 위한 세마포어
- `elem`: 부모의 children 리스트에 연결하기 위한 리스트 원소

부모는 자기 `children` 리스트에 자식의 `child_status`를 가지고 있다. 자식이 종료하면 자신의 `child_status`에 exit status를 기록하고, 부모가 `wait()` 중이면 깨운다.

## fork와 동기화

`fork()`는 현재 프로세스를 복제하는 작업이다.

중요한 점은 부모와 자식이 동시에 실행될 수 있기 때문에 동기화가 필요하다는 점이다.

현재 흐름에서 본 세마포어 역할은 다음과 같다.

- `start_sema`: 부모가 자식의 `tid`와 기본 상태를 준비한 뒤 자식을 진행시키기 위한 세마포어
- `done_sema`: 자식이 주소 공간과 파일 디스크립터 복제를 끝낼 때까지 부모가 기다리기 위한 세마포어
- `wait_sema`: 자식 종료를 부모의 `wait()`가 기다리기 위한 세마포어

즉 `fork`에서는 생성 순서, 복제 완료 시점, 종료 대기 시점을 분리해서 생각해야 한다.

## File Descriptor

파일을 열면 커널은 파일 객체를 직접 유저에게 주지 않고 정수 fd를 준다.

일반적으로 다음처럼 구분한다.

- `0`: stdin
- `1`: stdout
- `2` 이상: 열린 파일

프로세스는 자기 `fd_table`을 가지고 있고, syscall에서 fd를 받아 실제 파일 객체를 찾는다.

`write(fd=1)`은 콘솔 출력으로 처리할 수 있지만, `fd > 1`은 파일 객체를 찾아 `file_write()` 같은 파일 시스템 함수를 사용해야 한다.

## ROX

ROX는 실행 중인 파일에 대한 쓰기를 막는 기능이다.

실행 파일을 로드한 뒤에도 해당 파일 객체를 닫지 않고 들고 있어야 쓰기 금지 상태를 유지할 수 있다.

핵심 흐름은 다음과 같다.

```text
load 성공
-> file_deny_write()
-> running_file에 저장
-> process_exit()에서 file_allow_write()
-> file_close()
```

실행 중인 파일을 바로 닫아버리면 쓰기 금지 상태를 유지할 수 없기 때문에, 프로세스가 종료될 때까지 `running_file`로 관리한다.

## 테스트 관점

개념별로 확인할 수 있는 테스트 흐름은 다음처럼 정리할 수 있다.

- argument passing: `args-none`, `args-single`, `args-multiple`, `args-many`
- syscall 출력: `write(fd=1)`이 필요한 args 계열
- 유저 메모리 검증: 잘못된 포인터를 넘기는 bad pointer 계열
- exit/wait: exit status와 wait 결과 확인 계열
- file descriptor: open, read, write, close 계열
- ROX: `rox-simple`, `rox-child`, `rox-multichild`

테스트 실패를 볼 때는 단순히 PASS/FAIL만 보는 것이 아니라, 어느 syscall까지 도달했는지, 출력이 어디까지 나왔는지, kernel panic인지 timeout인지 구분해서 봐야 한다.

## 이번 주차에서 정리한 핵심 키워드

- `process_create_initd`
- `thread_create`
- `initd`
- `process_exec`
- `parse_command_line`
- `setup_argument_stack`
- `load`
- `intr_frame`
- `do_iret`
- `argc`
- `argv`
- `rsp`
- `rdi`
- `rsi`
- `syscall_handler`
- `syscall_entry`
- `dispatch_syscall`
- `syscall number`
- `user pointer`
- `user memory validation`
- `is_user_vaddr`
- `pml4_get_page`
- `page fault`
- `child_status`
- `process_wait`
- `process_exit`
- `semaphore`
- `fork`
- `file descriptor`
- `fd_table`
- `running_file`
- `file_deny_write`
- `file_allow_write`
- `ROX`

