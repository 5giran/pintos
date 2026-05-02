# PintOS Project 2 - Argument Passing Parsing 구현 기록

## 1. 맥락

Project 2의 첫 번째 관문은 커널이 사용자 프로그램을 실행할 때 명령줄을 실행 파일 이름과 인자로 나누고, 이후 사용자 스택에 `argc`와 `argv`를 올바르게 배치하는 것이다.

이번 기록은 그중 `process_exec()`에서 명령줄을 파싱하고, 파싱 결과를 `load()`로 전달하는 단계까지를 정리한다. 실제 사용자 스택에 문자열과 포인터 배열을 push하고 `RDI`, `RSI`, `RSP` 레지스터를 설정하는 작업은 아직 `load()` 내부 TODO로 남아 있다.

이번 Parsing 구현은 다음 커밋 흐름으로 진행되었다.

```text
9f20fc4 feat(arg pass): added command-line parsing logic in process_exec()
2697c6e feat(arg pass): changed load() function signature and added more comments in process_exec()
fd61cff refactor(arg pass): refactored tokenization logic in process_exec()
45ee843 fix(arg pass): fixed typo in tokenization logic
2d600bf refactor(arg pass): changed MAX_ARGS number to 32, completed parsing logic
```

현재 브랜치의 Parsing 구현 기준 파일은 `pintos/userprog/process.c`다.

## 2. 구현 목표

기존 skeleton은 `process_exec()`가 받은 `f_name` 전체를 그대로 `load()`에 넘기는 구조였다. 이 상태에서 다음 명령이 들어오면 문제가 생긴다.

```text
args-single onearg
```

실행 파일 이름은 `args-single`이어야 하지만, 파싱이 없으면 커널은 `args-single onearg`라는 파일을 열려고 한다. 따라서 `process_exec()`는 최소한 다음 정보를 만들어야 한다.

```text
argv_tokens[0] = "args-single"
argv_tokens[1] = "onearg"
argv_tokens[2] = NULL
argc = 2
```

이후 `load()`는 `argv_tokens[0]`으로 실행 파일을 열고, `argc`와 `argv_tokens`를 사용해 사용자 스택을 구성해야 한다.

## 3. 코드 수정한 부분

### `MAX_ARGS` 추가

최신 구현에서는 인자 개수 상한을 상수로 분리했다.

```c
#define MAX_ARGS 32
```

공개 테스트에서 가장 많은 인자를 사용하는 케이스는 `args-many`이고, 이때 `argc`는 23이다.

```text
args-many a b c d e f g h i j k l m n o p q r s t u v
argc = 23
```

처음에는 더 넉넉하게 64칸 배열을 검토했지만, 현재 구현은 공개 테스트 최대치 23보다 여유가 있는 32개 토큰을 상한으로 잡았다. 배열은 `argv_tokens[MAX_ARGS + 1]`로 선언한다. 마지막 한 칸은 `argv_tokens[argc] = NULL`을 저장하기 위한 자리다.

```c
char *argv_tokens[MAX_ARGS + 1];
```

주의: 구현 중 남아 있는 주석에 "64로 구현"이라는 표현이 있을 수 있다. 최신 코드 기준의 실제 상한은 `MAX_ARGS = 32`이므로, 빌드 전 주석도 상수와 맞게 정리하는 것이 좋다.

따라서 현재 의미는 다음과 같다.

```text
실제 토큰 최대 개수: MAX_ARGS
NULL sentinel 자리: argv_tokens[MAX_ARGS]
전체 배열 크기: MAX_ARGS + 1
```

### `load()` 시그니처 변경

기존 `load()`는 실행 파일 이름과 interrupt frame만 받았다.

```c
static bool load (const char *file_name, struct intr_frame *if_);
```

Argument Passing을 `load()` 내부에서 마무리하기로 결정하면서, `argc`와 `argv_tokens`도 함께 받도록 시그니처를 바꾸었다.

```c
static bool load (const char *file_name,
                  struct intr_frame *if_,
                  int argc,
                  char *argv_tokens[]);
```

이 변경의 의미는 다음과 같다.

```text
file_name: filesys_open()에 사용할 실행 파일 이름
if_: 사용자 모드 진입 시 복원할 CPU 상태
argc: 실제 인자 개수
argv_tokens: file_name 페이지 내부의 각 토큰 시작 주소 배열
```

함수 매개변수 자리에서 `char *argv_tokens[]`는 `char **argv_tokens`와 같은 의미다. `argv_tokens`는 `char *` 원소들을 담은 배열이고, 함수에는 그 배열의 첫 원소 주소가 전달된다.

### `process_exec()`에서 명령줄 토큰화

`process_exec()`는 `f_name`을 `char *file_name`으로 받아 사용한다.

```c
char *file_name = f_name;
```

이 `f_name`은 `process_create_initd()`에서 `palloc_get_page()`로 만든 `fn_copy` 페이지다. 따라서 `process_exec()` 안에서 `strtok_r()`로 직접 수정해도 된다. 원본 사용자 문자열을 직접 건드리는 것이 아니라, 커널이 소유한 수정 가능한 복사본을 쪼개는 것이다.

토큰화는 `strtok_r()`로 진행한다.

```c
for (token = strtok_r (file_name, " ", &next_token);
     token != NULL;
     token = strtok_r (NULL, " ", &next_token)) {
    ...
}
```

첫 호출에서는 토큰화할 문자열인 `file_name`을 넘기고, 이후 호출에서는 `NULL`을 넘긴다. 세 번째 인자인 `next_token`은 다음 탐색 위치를 기억하는 저장소다.

토큰을 저장하기 전에 `argc`가 상한에 도달했는지 검사한다.

```c
if (argc == MAX_ARGS) {
    palloc_free_page (file_name);
    return -1;
}
```

그 뒤 현재 토큰을 저장하고 `argc`를 증가시킨다.

```c
argv_tokens[argc] = token;
argc++;
```

루프가 끝나면 `argv_tokens[argc]`에 `NULL`을 넣는다.

```c
argv_tokens[argc] = NULL;
```

이 값은 커널 내부 임시 배열의 sentinel이다. 이후 사용자 스택을 만들 때도 같은 논리로 사용자 공간의 `argv[argc]`가 `NULL`이 되도록 별도의 null pointer를 push해야 한다.

### `load()` 호출부 변경

파싱 이후 `load()`에는 전체 명령줄이 아니라 실행 파일 이름인 `argv_tokens[0]`을 넘긴다.

```c
success = load (argv_tokens[0], &_if, argc, argv_tokens);
```

여기서 `argv_tokens[0]`은 첫 번째 토큰, 즉 실행 파일 이름이다. `argv_tokens`는 전체 토큰 배열이고, `load()` 내부에서 사용자 스택을 구성할 때 필요하다.

중요한 구분은 다음과 같다.

```text
argv_tokens[0] = 첫 번째 토큰 문자열 주소, 타입은 char *
argv_tokens = 토큰 포인터 배열의 시작 주소, 타입은 char **
&argv_tokens = 배열 전체의 주소, 타입은 char *(*)[MAX_ARGS + 1]
```

이번 구현에서는 실행 파일 이름과 인자 목록이 모두 필요하므로 `argv_tokens[0]`, `argc`, `argv_tokens`를 모두 넘긴다.

### `file_name` 해제 위치 유지

`file_name`은 `process_create_initd()`에서 할당된 `fn_copy` 페이지다. `process_exec()`에서 직접 할당하지 않았더라도 이 함수가 사용을 끝낸 뒤 해제해야 한다.

```c
palloc_free_page (file_name);
```

단, `argv_tokens[i]`는 모두 `file_name` 페이지 내부를 가리킨다. 그러므로 `load()`가 사용자 스택 구성을 끝내기 전에 `file_name`을 해제하면 안 된다. 현재 흐름에서는 `load()`가 반환한 뒤 `palloc_free_page(file_name)`을 호출하므로, `load()` 내부에서 인자 문자열을 사용자 스택에 복사하는 데 사용할 수 있다.

## 4. 구현하면서 확정한 흐름

현재 Parsing 이후의 전체 흐름은 다음을 목표로 한다.

```text
process_exec(f_name)
  file_name = f_name
  argv_tokens 준비
  strtok_r(file_name, " ", &next_token)로 토큰화
  argc 계산
  argv_tokens[argc] = NULL

  process_cleanup()
  success = load(argv_tokens[0], &_if, argc, argv_tokens)

  palloc_free_page(file_name)
  if !success:
      return -1
  do_iret(&_if)

load(file_name, if_, argc, argv_tokens)
  filesys_open(file_name)
  ELF header 검증
  PT_LOAD segment 매핑
  setup_stack(if_)으로 빈 사용자 스택 페이지 생성
  argv_tokens를 사용자 스택에 복사
  argv 포인터 배열과 NULL sentinel push
  fake return address push
  if_->R.rdi = argc
  if_->R.rsi = argv 시작 주소
  if_->rsp = 최종 스택 포인터
  if_->rip = ELF entry
```

현재 완료된 부분은 `process_exec()`의 파싱과 `load()`로 전달하는 부분이다. 다음 단계는 `load()` 내부에서 `setup_stack(if_)` 이후 실제 사용자 스택과 레지스터를 설정하는 것이다.

## 5. 커밋별 구현 변화

### `9f20fc4 feat(arg pass): added command-line parsing logic in process_exec()`

처음으로 `process_exec()`에 `argv_tokens` 배열, `strtok_r()` 호출, `argc` 계산 로직을 추가했다. 이 단계에서는 전체 명령줄을 토큰화하는 기본 흐름을 확인했다.

초기 구조는 다음 형태였다.

```text
첫 번째 token을 argv_tokens[0]에 저장
while(token)에서 다음 token을 계속 저장
마지막 NULL도 argv_tokens에 저장
argc는 반복이 끝난 뒤 i 값으로 계산
```

다만 이 시점에는 `load()`가 여전히 기존 시그니처였고, `load(file_name, &_if)` 호출이 남아 있었다.

### `2697c6e feat(arg pass): changed load() function signature and added more comments in process_exec()`

`load()`가 `argc`와 `argv_tokens`를 받을 수 있도록 시그니처를 변경했다. `process_exec()`에서는 `argv_tokens[0]`을 실행 파일 이름으로 넘기도록 호출부를 바꾸었다.

또한 `intr_frame _if`가 왜 필요한지, `SEL_UDSEG`, `SEL_UCSEG`, `FLAG_IF`, `FLAG_MBS`가 사용자 모드 진입 상태를 준비한다는 설명 주석을 추가했다.

이 커밋에서 중요한 설계 판단은 다음이다.

```text
process_exec()는 파싱한다.
load()는 실행 파일 로드와 사용자 초기 실행 상태 구성을 담당한다.
```

### `fd61cff refactor(arg pass): refactored tokenization logic in process_exec()`

토큰화 구조를 `for` 루프로 정리하고, `MAX_ARGS` 상수를 도입했다. 이 리팩토링의 목적은 토큰을 저장하기 전에 배열 상한을 검사하고, 루프 종료 뒤 `argv_tokens[argc] = NULL`을 한 번만 설정하는 구조로 만들기 위함이다.

이때 한 번 실수한 부분은 조건식이었다.

```c
if (argc = MAX_ARGS)
```

이 코드는 비교가 아니라 대입이므로 항상 실패 경로로 빠질 수 있다.

### `45ee843 fix(arg pass): fixed typo in tokenization logic`

위 조건식을 비교 연산으로 수정했다.

```c
if (argc == MAX_ARGS)
```

이 수정으로 `argc`가 실제 상한에 도달했을 때만 실패 처리하도록 의도에 맞게 동작한다.

### `2d600bf refactor(arg pass): changed MAX_ARGS number to 32, completed parsing logic`

`MAX_ARGS` 값을 63에서 32로 줄였다. 공개 테스트의 최대 `argc`가 23이므로, 32는 충분한 여유를 가지면서도 커널 스택에 올리는 임시 포인터 배열 크기를 더 작게 유지한다.

최신 구조에서는 다음이 성립한다.

```text
argv_tokens[0] ~ argv_tokens[argc - 1] = 실제 토큰
argv_tokens[argc] = NULL
argc <= MAX_ARGS
```

## 6. 현재 남은 작업

Parsing 단계 다음에는 `load()` 안에서 Argument Passing의 실제 스택 구성을 구현해야 한다.

남은 작업은 다음이다.

```text
1. argv_tokens 문자열들을 사용자 스택에 복사한다.
2. 각 문자열이 놓인 사용자 가상 주소를 기록한다.
3. 8바이트 정렬을 맞춘다.
4. argv[argc] = NULL sentinel을 push한다.
5. argv[argc - 1]부터 argv[0]까지 포인터를 역순으로 push한다.
6. fake return address를 push한다.
7. if_->R.rdi = argc를 설정한다.
8. if_->R.rsi = argv 시작 주소를 설정한다.
9. if_->rsp = 최종 스택 포인터를 설정한다.
10. 실패 시 success = true로 가지 않고 load 실패로 처리한다.
```

특히 `argv_tokens`에 들어 있는 주소는 커널 페이지 주소다. 사용자 프로그램의 `argv[i]`에 이 주소를 그대로 넣으면 안 된다. 문자열 내용을 사용자 스택에 복사하고, 그 사용자 가상 주소를 `argv[i]`로 넣어야 한다.
