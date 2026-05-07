# PintOS Project 2 - Argument Passing Loading 구현 기록

## 1. 맥락

이 문서는 `ts` 브랜치 기준으로 Argument Passing의 loading 단계를 정리한다. `ts` 브랜치의 최신 커밋은 다음이다.

```text
807c587 refactor(arg pass): allocate argument buffers with palloc
```

앞 단계에서는 `process_exec()`가 명령줄을 토큰화하고, 실행 파일 이름과 인자 배열을 `load()`에 넘기도록 만들었다. 이번 loading 단계에서는 `load()`가 ELF 파일을 로드한 뒤 사용자 스택에 문자열과 `argv` 포인터 배열을 배치하고, user mode 진입용 레지스터를 설정한다.

관련 커밋 흐름은 다음과 같다.

```text
07f13f1 feat(arg pass): implemented copying string to user stack logic in load()
073c0d6 feat(arg pass): rsp rounded down to 8-byte boundary
a99abe7 feat(arg pass): implemented argv[argc] = NULL on user stack
9913ae8 feat(arg pass): implemeneted pushing argc and argv to user stack and registers
807c587 refactor(arg pass): allocate argument buffers with palloc
```

## 2. ts 브랜치의 전체 구조

`ts` 브랜치에서는 parsing과 loading 책임이 분리되어 있다.

```text
process_exec()
  file_name 페이지를 strtok_r()로 토큰화
  palloc_get_page()로 argv_tokens 페이지 할당
  argv_tokens[0..argc-1] 준비
  argv_tokens[argc] = NULL 설정
  load(argv_tokens[0], &_if, argc, argv_tokens) 호출
  load() 반환 뒤 argv_tokens 페이지와 file_name 페이지 해제

load()
  argv_tokens[0]으로 실행 파일 열기
  palloc_get_page()로 arg_addr 페이지 할당
  ELF segment 로드
  setup_stack(if_)으로 빈 사용자 스택 생성
  argv_tokens 문자열을 사용자 스택에 복사
  argv 포인터 배열과 argv[argc] = NULL 구성
  RDI, RSI, RSP 설정
  done 경로에서 arg_addr 페이지 해제
```

현재 `load()` 시그니처는 다음이다.

```c
static bool load (const char *file_name,
                  struct intr_frame *if_,
                  int argc,
                  char *argv_tokens[]);
```

각 인자의 의미는 다음과 같다.

```text
file_name
  실행 파일 이름이다. process_exec()에서 argv_tokens[0]을 넘긴다.

if_
  user mode로 진입할 때 복원할 CPU 상태다.

argc
  실제 토큰 개수다.

argv_tokens
  file_name 페이지 내부의 각 토큰 시작 주소 배열이다.
```

## 3. process_exec()에서 load()로 넘기는 값

`process_exec()`는 명령줄을 먼저 파싱한다.

```c
char **argv_tokens = palloc_get_page (0);
if (argv_tokens == NULL) {
  palloc_free_page(file_name);
  return -1;
}

int argc = 0;

for (token = strtok_r (file_name, " ", &next_token);
     token != NULL;
     token = strtok_r (NULL, " ", &next_token)) {
  if (argc == MAX_ARGS) {
    palloc_free_page(file_name);
    palloc_free_page(argv_tokens);
    return -1;
  }

  argv_tokens[argc] = token;
  argc++;
}
argv_tokens[argc] = NULL;
```

최신 구현에서는 `argv_tokens`를 커널 스택 배열로 두지 않고 `palloc_get_page()`로 한 페이지를 할당한다. 그래서 `MAX_ARGS`도 한 페이지에 들어갈 수 있는 포인터 개수 기준으로 계산한다.

```c
#define MAX_ARGS PGSIZE / sizeof(char *) - 1
```

한 페이지가 4096바이트이고 포인터가 8바이트라면 포인터 칸은 512개다. 이 중 마지막 한 칸은 `argv_tokens[argc] = NULL` sentinel을 위해 남겨 두므로 실제 토큰 상한은 511개다.

그 다음 전체 명령줄이 아니라 실행 파일 이름인 `argv_tokens[0]`을 `load()`의 첫 번째 인자로 넘긴다.

```c
success = load (argv_tokens[0], &_if, argc, argv_tokens);
```

예를 들어 명령줄이 다음과 같다면:

```text
args-single hello world
```

`process_exec()`가 만드는 값은 다음이다.

```text
argc = 3
argv_tokens[0] = "args-single"
argv_tokens[1] = "hello"
argv_tokens[2] = "world"
argv_tokens[3] = NULL
```

`load()`는 `argv_tokens[0]`으로 실행 파일을 열고, 전체 `argv_tokens`를 사용해 사용자 스택을 만든다.

`load()`가 끝나면 `process_exec()`는 임시 페이지들을 해제한다.

```c
palloc_free_page (argv_tokens);
palloc_free_page (file_name);
```

## 4. load()에서 추가한 변수

`load()`는 사용자 스택 구성을 위해 다음 변수를 둔다.

```c
void **arg_addr = palloc_get_page (0);
if (arg_addr == NULL) {
  goto done;
}
void *argv_addr;
uintptr_t rsp = if_->rsp;
```

각 변수의 의미는 다음과 같다.

```text
arg_addr[i]
  argv_tokens[i] 문자열을 사용자 스택에 복사한 뒤의 사용자 주소다.

argv_addr
  사용자 스택 안에 만들어진 argv 포인터 배열의 시작 주소다.

rsp
  stack pointer 주소 산술을 하기 위한 지역 변수다.
```

`argv_tokens[i]`는 커널 페이지 안의 임시 문자열 주소다. 사용자 프로그램의 `argv[i]`에는 이 주소가 아니라 사용자 스택에 복사된 주소인 `arg_addr[i]`가 들어가야 한다.

`arg_addr`도 최신 구현에서는 커널 스택 배열이 아니라 `palloc_get_page()`로 할당한 페이지다. `load()`의 모든 실패/성공 경로는 `done:`으로 모이므로, `done:`에서 다음처럼 해제한다.

```c
if (arg_addr != NULL) {
  palloc_free_page (arg_addr);
}
```

## 5. 빈 사용자 스택 생성

ELF segment 로드가 끝나면 `setup_stack(if_)`을 호출한다.

```c
if (!setup_stack (if_))
  goto done;
```

`setup_stack()`은 `USER_STACK - PGSIZE`부터 `USER_STACK`까지 한 페이지를 매핑하고, 성공하면 `if_->rsp = USER_STACK`으로 설정한다.

```text
높은 주소
USER_STACK              setup_stack() 직후 if_->rsp
...
USER_STACK - PGSIZE
낮은 주소
```

스택은 높은 주소에서 낮은 주소로 자라므로, 값을 넣을 때는 먼저 `rsp`를 줄여 공간을 확보한 뒤 그 위치에 값을 쓴다.

## 6. 문자열 내용을 사용자 스택에 복사

`07f13f1`에서 문자열 복사 로직이 들어갔다.

```c
uintptr_t rsp = if_->rsp;

for (int i = argc - 1; i >= 0; i--) {
  int len = strlen(argv_tokens[i]) + 1;
  rsp -= len;
  if (rsp < USER_STACK - PGSIZE) {
    goto done;
  }
  memcpy((void *) rsp, argv_tokens[i], len);
  arg_addr[i] = (void *) rsp;
}
```

핵심은 다음이다.

```text
1. 문자열 크기만큼 rsp를 먼저 내린다.
2. argv_tokens[i] 문자열 내용을 사용자 스택에 복사한다.
3. 복사된 사용자 주소를 arg_addr[i]에 저장한다.
```

문자열은 `argc - 1`부터 `0`까지 역순으로 복사한다. 스택이 아래로 자라기 때문에 뒤쪽 인자부터 넣는 방식이 자연스럽고, 이후 `argv` 포인터 배열을 정상 순서로 만들기 쉽다.

복사 크기는 `strlen(argv_tokens[i]) + 1`이다. 마지막 `\0`까지 복사해야 사용자 프로그램이 C 문자열로 읽을 수 있다.

## 7. 8바이트 정렬

`073c0d6`에서 문자열 복사 후 `rsp`를 8바이트 경계로 내림 정렬했다.

```c
rsp -= (rsp % 8);
if (rsp < USER_STACK - PGSIZE) {
  goto done;
}
```

`args.c`는 `argv` 주소가 8바이트 정렬되어 있는지 확인한다.

```c
if (((unsigned long long) argv & 7) != 0)
  msg ("argv and stack must be word-aligned, actually %p", argv);
```

따라서 `argv` 포인터 배열을 만들기 전에 `rsp`를 8의 배수 주소로 맞춘다.

## 8. 사용자 스택의 `argv[argc] = NULL`

`a99abe7`에서 사용자 스택에 null pointer를 push했다.

```c
rsp -= 8;
if (rsp < USER_STACK - PGSIZE) {
  goto done;
}
*(uintptr_t *) rsp = 0;
```

이 값은 사용자 프로그램이 보는 `argv[argc]`다.

```text
argv[0] = "args-single"
argv[1] = "hello"
argv[2] = "world"
argv[3] = NULL
```

`process_exec()`의 `argv_tokens[argc] = NULL`은 커널 내부 임시 배열의 sentinel이고, 위 코드는 사용자 스택에 실제 null pointer를 저장하는 작업이다.

## 9. `argv` 포인터 배열 push

`9913ae8`에서 `arg_addr[i]` 값을 사용자 스택에 push해서 `argv` 포인터 배열을 만들었다.

```c
for (int i = argc - 1; i >= 0; i--) {
  rsp -= 8;
  if (rsp < USER_STACK - PGSIZE) {
    goto done;
  }
  *(char **) rsp = arg_addr[i];
}
```

여기서 저장하는 것은 문자열 내용이 아니라 문자열 주소다.

```text
arg_addr[0] -> 사용자 스택 안의 "args-single\0"
arg_addr[1] -> 사용자 스택 안의 "hello\0"
arg_addr[2] -> 사용자 스택 안의 "world\0"
```

포인터 배열도 역순으로 push한다. 그래야 최종 메모리에서 낮은 주소부터 다음 순서가 된다.

```text
argv[0]
argv[1]
argv[2]
argv[3] = NULL
```

포인터 배열을 모두 push한 직후의 `rsp`가 `argv` 배열 시작 주소다.

```c
argv_addr = (void *) rsp;
```

## 10. fake return address와 레지스터 설정

`argv_addr`를 저장한 뒤 fake return address를 push한다.

```c
rsp -= 8;
if (rsp < USER_STACK - PGSIZE) {
  goto done;
}
*(uintptr_t *) rsp = 0;
```

마지막으로 `intr_frame`에 user mode 진입 시 필요한 레지스터 값을 넣는다.

```c
if_->R.rdi = (uint64_t) argc;
if_->rsp = (uint64_t) rsp;
if_->R.rsi = (uint64_t) argv_addr;
```

의미는 다음과 같다.

```text
RDI
  argc

RSI
  argv 배열 시작 주소

RSP
  fake return address 위치
```

주의할 점은 `RSI`에 최종 `rsp`를 넣으면 안 된다는 것이다. fake return address를 push한 뒤의 `rsp`는 `argv` 시작 주소보다 8바이트 낮다. 그래서 fake return address를 push하기 전에 `argv_addr`를 저장하고, 그 값을 `RSI`에 넣는다.

## 11. 최종 스택 레이아웃

예시 명령:

```text
args-single hello world
```

최종 사용자 스택은 의미상 다음 형태가 된다.

```text
낮은 주소
0                         fake return address, 최종 RSP
arg_addr[0]               argv[0]
arg_addr[1]               argv[1]
arg_addr[2]               argv[2]
0                         argv[3] = NULL
padding                   8-byte alignment
"args-single\0"
"hello\0"
"world\0"
높은 주소
```

사용자 프로그램이 보는 값은 다음이다.

```text
argc = 3
argv[0] = "args-single"
argv[1] = "hello"
argv[2] = "world"
argv[3] = NULL
```

## 12. 테스트 관점에서 남은 조건

`ts` 브랜치의 argument passing stack loading 자체는 user stack과 register를 구성하는 단계까지 구현되어 있고, 최신 커밋에서 `argv_tokens`와 `arg_addr` 임시 버퍼도 `palloc_get_page()` 기반으로 옮겼다. 다만 실제 `args-*` 테스트 통과에는 별도 조건이 남아 있다.

```text
1. ts 브랜치의 process_wait()는 아직 정상 대기 구현이 아니라 return -1 stub이다.
2. ts 브랜치의 syscall_handler()는 아직 "system call!" 출력 후 thread_exit()하는 skeleton이다.
3. args.c는 stdout 출력과 exit 흐름을 사용하므로 write, exit syscall 구현이 필요하다.
4. 디버그 출력이 있다면 공식 .ck 기대 출력과 달라져 실패한다.
```

즉 이 문서는 argument passing loading 구현 기록이고, 테스트 통과를 위해서는 process wait와 syscall 쪽 구현도 이어서 필요하다.
