# PintOS User Program Execution Flow

이 문서는 `run "args-single onearg"` 같은 user program 테스트가 PintOS 안에서 어떤 흐름으로 실행되는지 정리한다.

처음에는 함수 이름보다 개념을 먼저 잡아야 한다. 특히 다음 네 가지를 구분해야 한다.

```text
host CLI
  우리가 macOS/Linux 터미널에서 make, pintos 명령을 치는 실제 개발 환경

PintOS kernel
  QEMU 안에서 부팅되는 작은 운영체제

kernel thread
  PintOS kernel 안에서 scheduler가 관리하는 실행 단위

user process
  사용자 프로그램을 실행하는 단위
  Project 2에서는 보통 "kernel thread 하나 + user address space 하나"로 생각하면 된다
```

핵심 함수 흐름은 다음 한 줄이다.

```text
run_actions() -> run_task() -> process_create_initd() -> 새 kernel thread의 initd()
              -> process_exec() -> load() -> do_iret() -> user mode main()
```

## 1. 먼저 잡아야 할 전체 그림

PintOS 테스트를 돌릴 때 우리는 host CLI에서 명령을 친다.

예를 들어 개념적으로는 다음처럼 테스트를 실행한다.

```bash
make tests/userprog/args-single.result
```

그러면 host에서 실행 중인 make/perl/pintos script가 QEMU를 띄우고, QEMU 안에서 PintOS를 부팅한다. 보통 userprog 테스트 하나는 PintOS를 한 번 부팅해서 그 테스트 하나를 실행하고 종료한다.

즉 일반 Linux shell처럼 이미 켜져 있는 OS 안에서 계속 명령을 입력하는 구조로 생각하면 헷갈린다. 테스트마다 대략 다음 일이 반복된다.

```text
host CLI에서 테스트 실행
  -> QEMU 시작
  -> PintOS kernel 부팅
  -> kernel command line에 적힌 action 실행
  -> user program 하나 실행
  -> 테스트 종료 후 power off
```

이때 `run "args-single onearg"`는 host shell 명령이 아니라, PintOS kernel에게 전달된 command line action이다.

## 2. kernel thread는 하나만 있는가?

아니다. PintOS kernel 안에는 여러 kernel thread가 있을 수 있다.

대표적으로 다음 thread들이 존재할 수 있다.

```text
main thread
  PintOS 부팅 후 init.c의 main(), run_actions(), run_task()를 실행하는 thread

idle thread
  실행할 다른 thread가 없을 때 CPU를 잡는 thread

user process용 thread
  사용자 프로그램 하나를 실행하기 위해 만들어지는 thread
```

Project 2에서 중요한 모델은 이것이다.

```text
user process = kernel thread 하나 + user address space 하나 + user program code/data/stack
```

즉 user process를 만든다는 말은, 커널 안에 완전히 별개의 "process 객체"만 만드는 게 아니다. 먼저 kernel thread 하나를 만들고, 그 thread에 page table과 user program image를 붙인 뒤, `do_iret()`로 user mode에 진입시킨다.

그래서 다음 표현이 중요하다.

```text
하나의 thread가 kernel mode에서 시작한다.
그 thread가 process_exec()/load()를 통해 user program 실행 상태를 준비한다.
do_iret() 이후 같은 실행 흐름이 user mode에서 사용자 프로그램을 실행한다.
```

`kernel process`라는 표현은 지금 단계에서는 쓰지 않는 편이 좋다. PintOS Project 2에서는 kernel 쪽 실행 단위는 보통 `kernel thread`로 생각한다.

`user thread`도 지금은 따로 복잡하게 생각하지 않아도 된다. 현재 단계에서는 대체로 다음처럼 보면 된다.

```text
user process 하나당 실행 thread 하나
```

## 3. 부트로더와 init.c의 관계

부트로더가 `run_actions()`를 직접 호출하는 것은 아니다.

실제 흐름은 다음에 가깝다.

```text
부트로더
  -> PintOS kernel을 메모리에 올림
  -> kernel entry/start 코드로 점프
  -> threads/init.c의 main() 실행
  -> thread, interrupt, filesystem 등 kernel 초기화
  -> read_command_line()
  -> parse_options()
  -> run_actions()
```

즉 `run_actions()`는 커널 초기화가 끝난 뒤, command line에 남아 있는 action을 실행하는 dispatcher다.

## 4. run_actions()가 보는 argv

테스트 명령이 개념적으로 다음과 같다고 하자.

```text
run "args-single onearg"
```

`parse_options()`가 `-q`, `-f` 같은 kernel option을 처리하고 나면 `run_actions()`는 대략 이런 배열을 받는다.

```c
argv[0] = "run";
argv[1] = "args-single onearg";
argv[2] = NULL;
```

`run_actions()`는 `argv[0]`의 action 이름을 `actions[]` 표에서 찾는다.

```c
{"run", 2, run_task}
```

이 항목은 다음 뜻이다.

```text
action 이름: "run"
필요한 argv 항목 수: 2개, action 이름 포함
실제로 호출할 함수: run_task
```

따라서 `"run"`을 찾으면 `run_task(argv)`를 호출한다.

`argv += a->argc`는 방금 처리한 action 묶음을 건너뛰는 코드다. action이 하나뿐이면 `argv`가 `NULL` sentinel을 가리키므로 while loop가 끝난다. action이 여러 개라면 다음 action을 계속 처리한다.

## 5. run_task()의 역할

`run_task()`는 `run` action의 실제 실행기다.

```c
static void
run_task (char **argv) {
  const char *task = argv[1];

  process_wait (process_create_initd (task));
}
```

여기서 `task`는 실행 파일 이름만이 아니다.

```text
task = "args-single onearg"
```

즉 사용자 프로그램 이름과 인자를 모두 포함한 command line 전체다.

Project 2 userprog 테스트에서는 `run_task()`가 다음 일을 한다.

```text
1. task 문자열을 꺼낸다.
2. process_create_initd(task)로 user process를 시작할 kernel thread를 만든다.
3. 반환된 tid를 process_wait(tid)에 넘겨 user process 종료까지 기다린다.
4. child가 끝나면 "Execution of ... complete."를 출력하고 반환한다.
```

여기서 가장 헷갈리는 문장이 2번이다.

```text
process_create_initd(task)로 user process를 시작할 kernel thread를 만든다.
```

이 말은 "kernel thread와 user process가 완전히 다른 두 실행 흐름으로 따로 돈다"는 뜻이 아니다.

뜻은 다음에 가깝다.

```text
새 kernel thread를 하나 만든다.
그 thread는 처음에는 kernel mode에서 initd()를 실행한다.
initd()가 process_exec()와 load()를 호출해 user program 실행 준비를 한다.
do_iret() 이후 그 thread의 실행 흐름이 user mode로 내려가 user program을 실행한다.
```

그래서 `process_create_initd()`는 "user process를 즉시 실행하는 함수"라기보다, "user process가 될 thread를 만드는 함수"에 가깝다.

## 6. process_create_initd()는 직접 실행 함수가 아니다

`process_create_initd()`의 이름 때문에 사용자 프로그램을 바로 실행하는 함수처럼 보일 수 있다. 하지만 실제 역할은 다르다.

이 함수는 새 kernel thread를 만들고, 그 thread가 나중에 `initd()`를 실행하게 한다.

```text
현재 thread, run_task() 쪽
  -> process_create_initd("args-single onearg")
    -> command line을 fn_copy page에 복사
    -> thread 이름으로 쓸 첫 토큰 "args-single" 추출
    -> thread_create("args-single", PRI_DEFAULT, initd, fn_copy)
    -> 새 thread의 tid 반환
```

중요한 점은 세 가지다.

```text
1. thread_create()는 새 kernel thread를 만들고 scheduler에 올린다.
2. 사용자 프로그램을 process_create_initd() 함수 안에서 바로 실행하지 않는다.
3. 새 thread는 initd(fn_copy)로 시작한다.
```

`fn_copy`에는 `"args-single onearg"` 전체 문자열이 들어 있다. 이 문자열은 나중에 `process_exec()`에서 parsing되고, `load()`에서 argument passing stack을 만드는 데 사용된다.

## 7. 새 thread에서 일어나는 일

새 thread가 스케줄되면 다음 흐름을 탄다.

```text
initd(fn_copy)
  -> process_exec(fn_copy)
    -> command line parsing
    -> load(argv[0], argc, argv_tokens)
      -> ELF load
      -> user stack 생성
      -> argument strings push
      -> argv pointer array push
      -> rdi = argc
      -> rsi = argv
      -> rsp = fake return address 위치
    -> do_iret()
      -> user mode로 전환
```

`do_iret()` 이후부터는 kernel mode가 아니라 user mode에서 테스트 프로그램의 `_start()`와 `main()`이 실행된다.

이 흐름을 "thread의 관점"에서 보면 다음과 같다.

```text
처음:
  새 thread는 kernel mode에서 initd()를 실행한다.

중간:
  process_exec()와 load()가 user program 실행에 필요한 메모리와 register를 준비한다.

전환:
  do_iret()가 준비된 intr_frame을 CPU에 복원한다.

이후:
  같은 thread의 실행 흐름이 user mode에서 args-single의 main(argc, argv)를 실행한다.
```

## 8. user program이 syscall을 호출하면 무슨 일이 생기는가

`do_iret()` 이후 user mode에서 `args-single` 같은 테스트 프로그램이 실행된다.

테스트 프로그램이 `msg()`를 호출하면 내부적으로 `write()` system call을 호출한다. user mode의 `write()`는 실제 파일 시스템이나 콘솔을 직접 만지는 함수가 아니다. syscall instruction을 통해 kernel mode로 다시 들어온다.

흐름은 대략 다음과 같다.

```text
user mode
  args-single main()
    -> msg()
      -> write()
        -> syscall instruction

kernel mode
  syscall_entry
    -> syscall_handler()
      -> SYS_WRITE 처리
      -> putbuf()로 console 출력
      -> user mode로 복귀
```

프로그램이 끝날 때도 비슷하다.

```text
user mode
  main() return
    -> _start()가 exit(status) 호출
      -> syscall instruction

kernel mode
  syscall_handler()
    -> SYS_EXIT 처리
    -> process_exit()
    -> thread_exit()
```

따라서 user program 실행 중에도 kernel mode와 user mode는 syscall을 기준으로 오간다.

## 9. 왜 process_wait()가 중요한가

`run_task()`는 이렇게 생겼다.

```c
process_wait (process_create_initd (task));
```

이를 두 줄로 풀면 다음과 같다.

```c
tid_t child = process_create_initd (task);
process_wait (child);
```

`process_create_initd()`는 child thread를 만들고 tid를 반환한다. 부모인 `run_task()` 쪽 main thread는 그 child가 끝날 때까지 기다려야 한다.

이 대기가 없으면 두 가지 문제가 생긴다.

```text
1. 부모가 사용자 프로그램 종료 전에 먼저 "Execution complete"를 찍거나 종료할 수 있다.
2. 테스트 harness가 기대하는 실행 경계와 종료 시점이 깨진다.
```

반대로 `process_wait()`가 현재처럼 무한 `thread_yield()`이면 child가 끝나도 부모가 빠져나오지 못한다.

```text
user program 종료
  -> process_wait()가 계속 yield
  -> run_task()가 끝나지 않음
  -> run_actions()가 끝나지 않음
  -> power_off까지 가지 못함
  -> 테스트 timeout
```

그래서 argument passing 테스트를 통과하려면 stack layout뿐 아니라 최소한의 `process_wait()`/`process_exit()` 동기화가 필요하다.

## 10. 한 번의 args-single 테스트 전체 흐름

`args-single` 테스트 하나만 놓고 전체 흐름을 끝까지 쓰면 다음과 같다.

```text
host CLI
  make tests/userprog/args-single.result

host scripts
  pintos script가 QEMU를 실행하고 kernel command line을 구성

PintOS boot
  부트로더가 kernel load
  init.c main() 실행
  kernel 초기화

kernel command line 처리
  read_command_line()
  parse_options()
  run_actions()
  run_task("args-single onearg")

첫 user process 생성
  process_create_initd("args-single onearg")
  thread_create("args-single", ..., initd, fn_copy)
  process_wait(child_tid)로 부모는 대기

child thread 실행
  initd(fn_copy)
  process_exec(fn_copy)
  load("args-single", argc, argv_tokens)
  do_iret()

user mode
  args-single main(argc, argv)
  write() syscall로 출력
  exit() syscall로 종료

kernel mode
  syscall_handler(SYS_EXIT)
  process_exit()
  thread_exit()
  부모 process_wait()가 깨어남

테스트 종료
  run_task() 반환
  run_actions() 반환
  -q 옵션이면 power_off()
```

## 11. 그림으로 본 역할 분리

```text
host CLI
  make / pintos command
    QEMU 안에서 PintOS를 부팅시킴

threads/init.c
  main()
    kernel 초기화

  run_actions()
    kernel command line action을 찾는 dispatcher

  run_task()
    "run XXX"에서 XXX를 꺼내 user process 생성을 요청하고 기다림

userprog/process.c
  process_create_initd()
    첫 user process가 될 kernel thread를 생성

  initd()
    새 thread의 시작 함수

  process_exec()
    command line을 parsing하고 현재 thread를 user process 실행 이미지로 바꿈

  load()
    ELF와 user stack, argc/argv를 구성

threads/thread.c
  do_iret()
    준비된 intr_frame으로 CPU 상태를 복원해 user mode로 진입

user mode
  tests/userprog/args.c
    main(argc, argv)
```

## 12. 지금 헷갈렸던 부분 요약

```text
Q. kernel thread는 하나만 있는가?
A. 아니다. PintOS kernel 안에는 main thread, idle thread, user process용 thread 등 여러 thread가 있을 수 있다.

Q. process_create_initd()가 user program을 바로 실행하는가?
A. 아니다. user program이 될 새 kernel thread를 만든다. 그 thread가 initd()에서 process_exec(), load(), do_iret()를 거쳐 user mode로 내려간다.

Q. user process와 kernel thread는 완전히 별개인가?
A. Project 2에서는 user process를 "kernel thread 하나가 user address space를 갖고 user mode로 내려간 것"으로 보면 이해하기 쉽다.

Q. 테스트 하나를 돌리면 PintOS 전체가 계속 켜져 있는가?
A. 보통은 아니다. 테스트 하나마다 QEMU에서 PintOS를 부팅하고, action 하나를 실행한 뒤 power off한다.

Q. process_wait()는 왜 필요한가?
A. 부모 thread가 child user process 종료까지 기다려야 run_task(), run_actions(), power_off까지 정상 진행되기 때문이다.
```

