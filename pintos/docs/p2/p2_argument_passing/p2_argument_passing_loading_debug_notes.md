# PintOS Project 2 - Argument Passing Loading Debug Notes

## 배경

이 문서는 `ts` 브랜치 기준으로 `load()`에서 사용자 스택과 레지스터를 구성하면서 헷갈렸던 지점을 정리한다. 기존 `p2_argument_passing_debug_notes.md`가 parsing과 전달 구조를 정리했다면, 이 문서는 `setup_stack()` 이후 실제 stack loading 단계에 집중한다.

현재 `ts` 브랜치의 흐름은 다음과 같다.

```text
process_exec()
  palloc_get_page()로 argv_tokens 페이지를 할당한다.
  argv_tokens와 argc를 만든다.
  load(argv_tokens[0], &_if, argc, argv_tokens)를 호출한다.
  load() 반환 뒤 argv_tokens와 file_name 페이지를 해제한다.

load()
  palloc_get_page()로 arg_addr 페이지를 할당한다.
  setup_stack() 이후 사용자 스택에 문자열과 argv 포인터 배열을 만든다.
  RDI, RSI, RSP를 설정한다.
  done 경로에서 arg_addr 페이지를 해제한다.
```

## palloc 변경 메모

최신 커밋 `807c587`에서 argument passing용 임시 배열을 커널 스택 배열에서 `palloc_get_page()` 할당으로 바꿨다.

이전 구조는 개념적으로 다음과 같았다.

```c
char *argv_tokens[MAX_ARGS + 1];
void *arg_addr[MAX_ARGS];
```

최신 구조는 다음과 같다.

```c
char **argv_tokens = palloc_get_page (0);
void **arg_addr = palloc_get_page (0);
```

이 변경의 의미는 다음과 같다.

```text
argv_tokens
  process_exec()에서 한 페이지를 할당한다.
  토큰 포인터들을 저장한다.
  load()가 반환한 뒤 process_exec()에서 해제한다.

arg_addr
  load()에서 한 페이지를 할당한다.
  사용자 스택에 복사된 문자열 주소들을 저장한다.
  load()의 done 경로에서 해제한다.
```

`MAX_ARGS`도 고정값 32가 아니라 한 페이지에 들어가는 포인터 개수 기준으로 바뀌었다.

```c
#define MAX_ARGS PGSIZE / sizeof(char *) - 1
```

한 페이지가 4096바이트이고 포인터가 8바이트라면 512칸을 담을 수 있다. 그중 한 칸은 `argv_tokens[argc] = NULL` sentinel을 위해 남겨 두므로 실제 토큰 상한은 511개다.

이제 중요한 실패 처리 규칙은 다음이다.

```text
argv_tokens 할당 실패
  file_name을 해제하고 process_exec() 실패 반환

인자가 MAX_ARGS에 도달
  file_name과 argv_tokens를 모두 해제하고 실패 반환

load() 내부 arg_addr 할당 실패
  done으로 이동해 load 실패 반환

load() 종료
  성공/실패와 관계없이 arg_addr를 해제
```

## 1. `memcpy()`의 첫 번째 인자는 목적지다

첫째, `memcpy()`의 첫 번째 인자는 복사할 목적지 주소인데, `rsp`를 줄이지 않고 그대로 쓰면 모든 인자를 같은 위치에 덮어쓰게 된다. 스택에 뭔가를 넣으려면 먼저 `rsp`를 아래로 내려야 한다.

잘못 생각하기 쉬운 형태는 다음이다.

```c
memcpy ((void *) rsp, argv_tokens[i], len);
```

이 코드 자체가 항상 틀린 것은 아니지만, 이 전에 `rsp -= len`으로 공간을 확보하지 않았다면 문제다. `setup_stack()` 직후 `rsp`는 `USER_STACK`이고, 이 주소는 스택 페이지의 맨 위다. 문자열을 넣을 공간은 그 아래쪽에 만들어야 한다.

현재 `ts` 구현의 순서는 다음이다.

```c
int len = strlen(argv_tokens[i]) + 1;
rsp -= len;
if (rsp < USER_STACK - PGSIZE) {
  goto done;
}
memcpy((void *) rsp, argv_tokens[i], len);
arg_addr[i] = (void *) rsp;
```

정리하면 순서는 항상 다음이어야 한다.

```text
1. 넣을 데이터 크기를 계산한다.
2. rsp를 그 크기만큼 낮춘다.
3. 스택 페이지 경계를 확인한다.
4. 그 주소에 데이터를 쓴다.
5. 필요하면 그 사용자 주소를 저장한다.
```

## 2. `sizeof(char *)`가 아니라 `strlen(...) + 1`

둘째, 복사할 크기가 틀렸었다. `sizeof(char *)`는 포인터 크기다. 64비트에서는 8바이트다. 그런데 문자열 복사 단계에서 복사해야 하는 것은 포인터가 아니라 문자열 내용 자체다.

예를 들어 `"args-single"`이면 복사해야 하는 크기는 다음이다.

```c
strlen("args-single") + 1
```

마지막 `\0`까지 포함해야 하니까 `+ 1`이 필요하다.

문자열 내용 복사와 포인터 저장을 구분해야 한다.

```text
문자열 내용 복사
  strlen(argv_tokens[i]) + 1 바이트

포인터 하나 저장
  8바이트
```

`sizeof(char *)`로 `"args-single"`을 복사하면 8바이트만 복사된다. `"args-single"`은 null terminator까지 12바이트이므로 문자열이 잘린다.

## 3. 문자열 복사 후 `arg_addr[i]`를 저장해야 한다

셋째, 각 문자열을 복사한 뒤 그 사용자 스택 주소를 기록해야 한다. 나중에 `argv[0]`, `argv[1]` 포인터 배열을 만들 때 필요하기 때문이다.

이 단계의 목표는 다음이다.

```text
argv_tokens[i]가 가리키는 문자열을 사용자 스택에 복사한다.
복사된 위치를 arg_addr[i]에 저장한다.
```

현재 구현은 다음처럼 한다.

```c
memcpy((void *) rsp, argv_tokens[i], len);
arg_addr[i] = (void *) rsp;
```

여기서 주소의 의미가 다르다.

```text
argv_tokens[i]
  process_exec()가 토큰화한 커널 페이지 안 문자열 주소다.

arg_addr[i]
  load()가 사용자 스택에 복사한 문자열의 사용자 주소다.
```

사용자 프로그램의 `argv[i]`에는 `argv_tokens[i]`가 아니라 `arg_addr[i]`가 들어가야 한다. `argv_tokens[i]`는 커널 쪽 임시 버퍼 주소이기 때문이다.

## 4. `*(uint64_t *) rsp = 0;`는 왜 필요한가

질문:

```text
Q. *(uint64_t *) rsp = 0; ? 왜? *rsp = NULL 이건 안되나?
```

답은 `rsp`의 타입과 저장하려는 값의 크기 때문이다.

`ts` 구현에서는 `rsp`를 주소 산술을 위해 `uintptr_t` 지역 변수로 둔다.

```c
uintptr_t rsp = if_->rsp;
```

이 값은 포인터가 아니라 주소를 담은 정수다. 따라서 `*rsp = NULL`처럼 바로 역참조할 수 없다. 먼저 이 정수 주소를 "어떤 타입의 값을 저장할 포인터"로 해석해야 한다.

null pointer 하나를 사용자 스택에 저장하려면 64비트 환경에서 8바이트 0을 써야 한다.

```c
*(uintptr_t *) rsp = 0;
```

또는 64비트 PintOS에서는 다음도 같은 효과다.

```c
*(uint64_t *) rsp = 0;
```

의미를 풀면 다음과 같다.

```text
(uintptr_t *) rsp
  rsp 주소를 uintptr_t 값 하나가 저장될 메모리 칸의 주소로 해석한다.

*(uintptr_t *) rsp
  그 메모리 칸 자체다.

= 0
  그 칸에 null pointer 값인 0을 저장한다.
```

오른쪽 값으로 `NULL`을 쓸 수도 있지만, 여기서는 포인터 변수에 대입하는 것이 아니라 사용자 스택의 8바이트 메모리 칸을 0으로 채우는 작업이다. 그래서 `0`이 더 직접적이다.

## 5. `argv_tokens[argc] = NULL`과 user stack NULL은 다르다

`process_exec()`에서 다음 코드를 넣었다.

```c
argv_tokens[argc] = NULL;
```

이 값은 커널 내부 임시 배열의 끝 표시다. 사용자 프로그램이 보는 `argv[argc]`가 자동으로 만들어지는 것은 아니다.

사용자 프로그램은 `RSI`가 가리키는 user stack 안의 `argv` 배열을 본다. 그래서 `load()` 안에서 별도로 null pointer를 push해야 한다.

```c
rsp -= 8;
*(uintptr_t *) rsp = 0;
```

이 위치가 최종적으로 사용자 프로그램의 `argv[argc]`가 된다.

## 6. 문자열 내용과 문자열 주소를 구분해야 한다

Argument Passing에서 사용자 스택에는 두 종류의 데이터가 들어간다.

```text
1. 문자열 내용
   "args-single\0", "hello\0", "world\0"

2. 문자열 주소
   argv[0], argv[1], argv[2]에 들어갈 포인터 값
```

문자열 내용은 `memcpy()`로 복사한다.

```c
memcpy((void *) rsp, argv_tokens[i], len);
```

문자열 주소는 포인터 크기만큼 공간을 만들고 저장한다.

```c
rsp -= 8;
*(char **) rsp = arg_addr[i];
```

`argv` 배열에 들어가는 것은 문자열 자체가 아니라 문자열이 놓인 사용자 주소다.

## 7. 왜 역순으로 push하는가

스택은 높은 주소에서 낮은 주소로 자란다. 값을 push할 때마다 `rsp`가 감소한다.

최종적으로 사용자 프로그램이 보는 `argv` 배열은 낮은 주소부터 다음 순서여야 한다.

```text
argv[0]
argv[1]
argv[2]
...
argv[argc] = NULL
```

따라서 push 순서는 반대다.

```text
push argv[argc] = NULL
push argv[argc - 1]
push argv[argc - 2]
...
push argv[0]
```

현재 `ts` 구현도 포인터 배열을 만들 때 `argc - 1`부터 `0`까지 내려온다.

```c
for (int i = argc - 1; i >= 0; i--) {
  rsp -= 8;
  *(char **) rsp = arg_addr[i];
}
```

## 8. `argv_addr`를 따로 저장해야 하는 이유

포인터 배열을 모두 push한 직후의 `rsp`가 `argv` 배열 시작 주소다.

```c
argv_addr = (void *) rsp;
```

그 다음 fake return address를 push한다.

```c
rsp -= 8;
*(uintptr_t *) rsp = 0;
```

fake return address를 push한 뒤의 `rsp`는 `argv` 배열 시작 주소가 아니라 그보다 8바이트 낮은 주소다. 따라서 `RSI`에 최종 `rsp`를 넣으면 안 된다.

```text
argv_addr
  argv 배열 시작 주소, RSI에 넣을 값

rsp
  fake return address 위치, 최종 RSP
```

마지막 설정은 다음처럼 구분된다.

```c
if_->R.rdi = (uint64_t) argc;
if_->rsp = (uint64_t) rsp;
if_->R.rsi = (uint64_t) argv_addr;
```

## 9. `argc`는 스택에 push하지 않고 `RDI`에 넣는다

32비트 PintOS 설명을 보면 `argc`, `argv`, fake return address를 모두 스택에 push하는 그림이 나온다. 하지만 KAIST PintOS x86-64에서는 함수 인자 전달 규칙에 따라 `argc`와 `argv`를 레지스터로 넘긴다.

따라서 `ts` 구현에서는 `argc`를 사용자 스택에 push하지 않는다.

```c
if_->R.rdi = (uint64_t) argc;
```

`argv` 배열 자체는 사용자 메모리에 있어야 하므로 스택에 만들고, 그 시작 주소를 `RSI`에 넣는다.

```c
if_->R.rsi = (uint64_t) argv_addr;
```

## 10. `if_`, `_if`, `do_iret()` 연결

`process_exec()`는 지역 변수 `_if`를 만든다.

```c
struct intr_frame _if;
```

`load()`는 이 구조체의 주소를 `if_`로 받는다.

```c
load (argv_tokens[0], &_if, argc, argv_tokens);
```

`if`는 C 예약어라 변수 이름으로 쓸 수 없으므로 `if_`라는 이름을 쓴다.

`load()`가 채우는 필드는 다음이다.

```text
if_->rip
  ELF entry point

if_->rsp
  fake return address 위치

if_->R.rdi
  argc

if_->R.rsi
  argv 배열 시작 주소
```

마지막에 `do_iret(&_if)`가 실행되면 이 값들이 CPU 레지스터로 복원되면서 user mode로 진입한다.

## 11. 스택 페이지 경계 검사

`setup_stack()`은 현재 사용자 스택을 한 페이지만 매핑한다.

```text
USER_STACK - PGSIZE <= 유효한 스택 주소 < USER_STACK
```

그래서 `rsp`를 내릴 때마다 다음 검사가 필요하다.

```c
if (rsp < USER_STACK - PGSIZE) {
  goto done;
}
```

`ts` 구현은 문자열, 정렬 이후, null pointer, argv 포인터, fake return address 단계에서 경계를 확인한다. 이 검사가 없으면 긴 명령줄이나 많은 인자에서 매핑되지 않은 사용자 주소에 쓰게 된다.

## 12. 디버깅 출력으로 확인할 점

argument passing을 확인할 때는 다음을 보면 된다.

```text
argc가 예상 개수인가
argv 주소가 8바이트 정렬되어 있는가
argv[0..argc-1]이 각 문자열 주소를 가리키는가
argv[argc]가 null pointer인가
RSP가 fake return address 위치인가
문자열들이 \0으로 끝나는가
```

단, 공식 `.ck` 테스트는 출력 비교가 엄격하다. 디버깅용 `printf`, `hex_dump`, 임시 로그가 남아 있으면 argument passing이 맞아도 실패한다.

## 13. 테스트가 아직 실패할 수 있는 이유

`ts` 브랜치 기준으로 stack loading은 구현되어 있지만, `args-*` 테스트를 실제로 통과하려면 다른 조건도 필요하다.

현재 `ts`의 `process_wait()`는 정상 대기 구현이 아니라 stub이다.

```c
return -1;
```

또한 `ts`의 `syscall_handler()`는 아직 skeleton이다.

```c
printf ("system call!\n");
thread_exit ();
```

`args.c`는 `msg()`로 stdout에 출력하고 `_start()`가 `exit(main(argc, argv))`를 호출한다. 따라서 실제 테스트 통과에는 최소한 다음이 필요하다.

```text
write(fd=1)
exit(status)
process_wait()의 정상 대기/반환
불필요한 디버그 출력 제거
```

## 14. 현재 확정한 Loading 규칙

`ts` 브랜치 기준으로 확정한 규칙은 다음과 같다.

```text
process_exec()가 palloc_get_page()로 argv_tokens 페이지를 할당한다.
process_exec()가 argv_tokens와 argc를 만든다.
load()에는 argv_tokens[0], &_if, argc, argv_tokens를 넘긴다.
load()가 palloc_get_page()로 arg_addr 페이지를 할당한다.
문자열 내용은 strlen(...) + 1 크기로 사용자 스택에 복사한다.
복사 전에는 반드시 rsp를 먼저 내린다.
복사된 사용자 주소는 arg_addr[i]에 저장한다.
argv 포인터 배열에는 arg_addr[i]를 넣는다.
argv[argc] = NULL은 사용자 스택에 8바이트 0으로 직접 push한다.
argv 배열 시작 주소는 argv_addr에 저장한 뒤 RSI에 넣는다.
argc는 RDI에 넣는다.
최종 RSP는 fake return address 위치다.
load() 종료 시 arg_addr를 해제한다.
process_exec()에서 load() 반환 뒤 argv_tokens와 file_name을 해제한다.
```
