# PintOS Project 2 - Argument Passing Debug Notes

## 배경

Argument Passing을 구현하면서 가장 많이 헷갈린 부분은 "명령줄을 어디까지 파싱하고, 언제 사용자 스택에 복사해야 하는가"였다. 특히 `argv_tokens` 배열의 크기, `strtok_r()`가 원본 문자열을 수정한다는 점, `load()`에 무엇을 넘겨야 하는지, 그리고 사용자 스택의 `argv[argc] = NULL` 의미가 반복적으로 확인되었다.

이 문서는 구현 중 질문이 나왔던 지점과 확정한 판단을 정리한다. 코드 정답을 외우기 위한 문서가 아니라, 같은 혼동을 다시 줄이기 위한 기록이다.

## 1. `argv_tokens` 배열은 어떻게 선언해야 하는가

`argv_tokens`는 문자열 내용을 담는 배열이 아니라, 각 토큰 문자열의 시작 주소를 담는 포인터 배열이다.

```text
argv_tokens[0] -> "args-many"
argv_tokens[1] -> "a"
argv_tokens[2] -> "b"
...
```

따라서 배열 원소 타입은 `char *`다. 즉 `argv_tokens`는 `char *` 배열이다.

최신 구현에서는 `argv_tokens`를 지역 배열로 두지 않고 `palloc_get_page()`로 한 페이지를 할당한다.

```c
char **argv_tokens = palloc_get_page (0);
```

처음에는 고정 크기 배열로 충분하다고 판단했다. 이유는 다음과 같았다.

- PintOS 명령줄은 `process_create_initd()`에서 한 페이지 복사본으로 전달된다.
- 공개 테스트에서 가장 많은 인자를 사용하는 `args-many`의 `argc`는 23이다.
- `argv_tokens`에는 문자열 전체가 아니라 포인터만 들어간다.
- 사용자 스택에는 실제 `argc`만큼만 복사하므로, 배열 capacity가 크다고 그만큼 사용자 스택을 차지하지 않는다.

이후 `807c587`에서 임시 포인터 배열을 커널 스택에 두지 않고 `palloc` 페이지로 옮겼다. 이에 따라 `MAX_ARGS`도 고정값 32가 아니라 한 페이지에 들어가는 포인터 개수 기준으로 바뀌었다.

```c
#define MAX_ARGS PGSIZE / sizeof(char *) - 1
```

한 페이지가 4096바이트이고 포인터가 8바이트라면 512칸을 담을 수 있다. 이 중 한 칸은 `argv_tokens[argc] = NULL` sentinel을 위해 남겨 두므로 실제 토큰 상한은 511개다.

중요한 점은 배열 크기와 실제 사용량이 다르다는 것이다.

```text
할당 capacity: palloc_get_page()로 받은 1페이지
실제 토큰 개수: argc
사용자 스택에 복사할 문자열: argv_tokens[0]부터 argv_tokens[argc - 1]까지만
NULL sentinel: argv_tokens[argc]
```

예를 들어 인자가 2개뿐이면 `argv_tokens[0]`, `argv_tokens[1]`, `argv_tokens[2]`만 의미가 있다. 나머지 배열 칸은 사용자 스택에 복사하지 않는다.

## 2. `argv_tokens[argc] = NULL`은 왜 필요한가

C 프로그램에서 `argv`는 마지막 원소가 `NULL`이어야 한다.

```text
argc = 4
argv[0] = "/bin/ls"
argv[1] = "-l"
argv[2] = "foo"
argv[3] = "bar"
argv[4] = NULL
```

여기서 `argv[4]`가 `argv[argc]`다. 이 값은 프로그램이 `argv`를 순회할 때 끝을 판단하는 sentinel 역할을 한다.

커널 내부의 `argv_tokens[argc] = NULL`은 사용자 스택의 `argv[argc] = NULL`을 만들기 위한 임시 표현이다. 하지만 둘은 위치가 다르다.

```text
argv_tokens[argc] = NULL
  커널 내부 임시 배열의 sentinel

user stack의 argv[argc] = NULL
  사용자 프로그램이 실제로 보는 argv 배열의 마지막 원소
```

따라서 `argv_tokens`에 NULL을 넣었다고 해서 사용자 스택의 NULL sentinel이 자동으로 생기는 것은 아니다. `load()` 내부에서 사용자 스택에 포인터 배열을 만들 때 별도로 null pointer를 push해야 한다.

## 3. 왜 포인터 배열은 역순으로 push하는가

스택은 높은 주소에서 낮은 주소로 자란다. 즉 값을 push할 때마다 `rsp`가 감소한다.

최종적으로 사용자 프로그램이 보는 `argv` 배열은 낮은 주소에서 높은 주소 방향으로 다음 순서여야 한다.

```text
낮은 주소
argv[0]
argv[1]
argv[2]
...
argv[argc] = NULL
높은 주소
```

따라서 스택에 push할 때는 반대 순서로 넣어야 한다.

```text
push NULL
push argv[argc - 1]
push argv[argc - 2]
...
push argv[0]
```

이렇게 해야 마지막에 push한 `argv[0]`이 가장 낮은 주소에 놓이고, 그 주소를 `argv` 배열의 시작 주소로 `RSI`에 넣을 수 있다.

## 4. `process_exec()`에서 어디까지 하고, `load()`에서 어디까지 해야 하는가

구현 방향은 다음처럼 결정했다.

```text
process_exec()
  명령줄 파싱
  실행 파일 이름과 인자 목록 준비
  load(argv_tokens[0], &_if, argc, argv_tokens) 호출

load()
  실행 파일 로드
  빈 사용자 스택 페이지 생성
  사용자 스택에 argc/argv 구성
  user mode 진입용 레지스터 설정
```

이렇게 나눈 이유는 책임 분리 때문이다.

`process_exec()`는 실행 컨텍스트 전환의 큰 흐름을 담당한다. 반면 `load()`는 ELF 세그먼트 매핑, 빈 스택 생성, `rip`, `rsp` 설정처럼 사용자 프로그램이 시작 가능한 상태를 만드는 역할을 이미 맡고 있다. Argument Passing도 사용자 프로그램의 초기 실행 상태를 만드는 작업이므로 `load()` 안에서 끝내는 편이 더 자연스럽다.

따라서 `setup_stack()` 자체는 고치지 않고, `setup_stack()`이 빈 스택 페이지를 만든 뒤 별도 argument helper를 호출하는 방향이 좋다.

```text
setup_stack() = 빈 사용자 스택 페이지 생성
argument helper = 그 스택 위에 문자열, argv 포인터 배열, fake return address 배치
```

## 5. `file_name` 원본을 `strtok_r()`로 수정해도 되는가

우리 맥락에서는 괜찮다. 단, 중요한 전제가 있다.

```text
process_exec()의 f_name은 원본 사용자 문자열이 아니라,
process_create_initd()에서 palloc_get_page()로 만든 커널 페이지 복사본이다.
```

실제 흐름은 다음과 같다.

```text
process_create_initd()
  fn_copy = palloc_get_page(0)
  strlcpy(fn_copy, file_name, PGSIZE)
  thread_create(..., initd, fn_copy)

initd(f_name)
  process_exec(f_name)
```

따라서 `process_exec()`에서 받는 `file_name`은 수정 가능한 커널 페이지다. `strtok_r()`는 공백 문자를 `'\0'`으로 바꾸는데, 이 복사본은 어차피 나중에 해제할 임시 버퍼이므로 직접 토큰화해도 된다.

동료가 새 페이지를 하나 더 할당해서 복사한 뒤 파싱하는 방식도 안전하지만, 현재 단계에서는 중복에 가깝다. 이미 있는 `file_name` 페이지가 다음 역할을 모두 할 수 있다.

```text
1. strtok_r()의 토큰화 대상
2. argv_tokens[i]가 가리키는 임시 문자열 저장소
3. load()가 사용자 스택에 문자열을 복사할 원본
```

단, `load()`가 사용자 스택 구성을 끝내기 전에 `file_name`을 해제하면 안 된다.

## 6. `palloc_free_page(file_name)`과 `palloc_free_page(argv_tokens)`가 필요한가

둘 다 필요하다. `file_name`은 `process_exec()` 안에서 직접 할당한 페이지는 아니지만, `f_name`은 위 호출 경로에서 이미 할당된 페이지다.

```text
f_name == file_name
```

`process_exec()` 안의 다음 대입은 같은 주소에 다른 이름을 붙인 것뿐이다.

```c
char *file_name = f_name;
```

따라서 다음 호출은 `f_name`을 해제하는 것과 같은 의미다.

```c
palloc_free_page (file_name);
```

`strtok_r()`는 문자열 내부의 공백을 `'\0'`으로 바꿀 뿐, `file_name` 포인터 변수 자체를 다른 주소로 바꾸지 않는다. 그러므로 `palloc_free_page(file_name)`은 여전히 올바른 페이지 시작 주소를 해제한다.

최신 구현에서는 `argv_tokens`도 `process_exec()`에서 직접 할당한다.

```c
char **argv_tokens = palloc_get_page (0);
```

따라서 `load()`가 반환한 뒤에는 `argv_tokens`도 함께 해제해야 한다.

```c
palloc_free_page (argv_tokens);
palloc_free_page (file_name);
```

## 7. `load()`에는 정확히 무엇을 넘겨야 하는가

`load()`에는 다음 정보가 필요하다.

```text
1. 실행 파일 이름
2. intr_frame 주소
3. argc
4. argv_tokens 배열 전체
```

현재 호출 형태는 다음을 목표로 한다.

```c
success = load (argv_tokens[0], &_if, argc, argv_tokens);
```

각 인자의 의미는 다음과 같다.

```text
argv_tokens[0]
  실행 파일 이름이다. filesys_open()에 사용한다.

&_if
  user mode로 넘어갈 때 복원할 CPU 상태 구조체의 주소다.

argc
  실제 토큰 개수다.

argv_tokens
  전체 토큰 포인터 배열이다. load() 내부에서 사용자 스택에 문자열을 복사할 때 사용한다.
```

`argv_tokens[0]`만 넘기면 첫 번째 토큰 하나만 전달된다. 배열 전체가 넘어가는 것이 아니다. 배열 전체를 순회해야 하는 `load()` helper에는 `argv_tokens`도 함께 넘겨야 한다.

## 8. `argv_tokens`, `argv_tokens[0]`, `&argv_tokens`의 차이

최신 구현의 선언은 다음과 같다.

```c
char **argv_tokens = palloc_get_page (0);
```

각 표현의 의미는 다르다.

```text
argv_tokens[0]
  첫 번째 토큰 문자열의 시작 주소
  타입: char *

argv_tokens
  토큰 포인터들이 저장된 페이지의 시작 주소
  타입: char **

&argv_tokens
  argv_tokens 변수 자체의 주소
  타입: char ***
```

`load()`가 필요한 것은 토큰 포인터 배열을 순회할 수 있는 페이지 시작 주소이므로 `argv_tokens`를 넘긴다. `&argv_tokens`는 `argv_tokens` 지역 변수 자체의 주소라서 이번 목적과 맞지 않는다.

## 9. 파싱 후에도 `file_name`을 `load()`에 넘겨도 되는가

단순한 입력에서는 우연히 동작할 수 있다.

예를 들어 입력이 다음과 같다면:

```text
args-single onearg
```

`strtok_r()` 이후 메모리는 대략 다음처럼 바뀐다.

```text
args-single\0onearg\0
^
file_name
^
argv_tokens[0]
```

이 경우 `file_name`과 `argv_tokens[0]`이 같은 주소를 가리키므로 `load(file_name, ...)`도 실행 파일 이름만 보는 것처럼 동작할 수 있다. C 문자열은 `'\0'`에서 끝나기 때문이다.

하지만 앞쪽에 공백이 있으면 달라진다.

```text
"   args-single onearg"
```

`strtok_r()`는 앞 공백을 건너뛰고 첫 토큰 시작 주소를 반환한다.

```text
file_name      -> 버퍼의 시작, 앞 공백 위치
argv_tokens[0] -> "args-single"
```

따라서 의미상 정확한 값은 항상 `argv_tokens[0]`이다. `load()`에서 실행 파일을 열 때는 `argv_tokens[0]`을 사용해야 한다.

## 10. `strtok`과 `strtok_r`의 차이

핵심 차이는 토큰화 진행 상태를 어디에 저장하느냐다.

`strtok`은 함수 내부의 static 상태에 진행 위치를 저장한다. 그래서 여러 문자열을 동시에 토큰화하거나 여러 스레드에서 사용하면 상태가 꼬일 수 있다.

`strtok_r`은 진행 위치를 호출자가 넘긴 세 번째 인자에 저장한다.

```c
char *save_ptr;

token = strtok_r (file_name, " ", &save_ptr);
token = strtok_r (NULL, " ", &save_ptr);
```

첫 호출에서는 원본 문자열을 넘기고, 이후 호출에서는 `NULL`을 넘긴다. `NULL`은 "새 문자열이 아니라 `save_ptr`가 기억하는 위치에서 이어서 찾으라"는 의미다.

PintOS에서는 커널 안에서 여러 스레드가 실행될 수 있으므로 `strtok`보다 `strtok_r`이 적합하다. `include/lib/string.h`에서도 `strtok`을 직접 쓰지 말고 `strtok_r`을 쓰도록 유도한다.

## 11. `__strtok_r`를 써야 하는가

쓰지 않는다. PintOS 코드에는 `strtok_r`가 선언되어 있다.

```text
pintos/include/lib/string.h
  char *strtok_r (char *, const char *, char **);
```

`__`가 붙은 이름은 보통 libc나 컴파일러 내부 구현용 예약 이름이다. 일반 PintOS 코드에서는 `strtok_r`를 사용하면 된다.

## 12. `if_`는 무엇인가

`if_`는 `struct intr_frame`의 주소다. 사용자 프로그램으로 넘어갈 때 CPU 레지스터 상태를 미리 담아두는 구조체라고 보면 된다.

`process_exec()`에서는 지역 변수로 `_if`를 만들고, 그 주소를 `load()`에 넘긴다.

```c
struct intr_frame _if;
load (..., &_if, ...);
```

`load()` 안에서는 이 주소를 보통 `if_`라는 이름으로 받는다. `if`는 C 예약어라 변수 이름으로 쓸 수 없기 때문에 뒤에 `_`를 붙인 것이다.

Argument Passing에서는 이 구조체의 다음 필드를 설정해야 한다.

```text
if_->rip = 사용자 프로그램 시작 주소
if_->rsp = 최종 사용자 스택 포인터
if_->R.rdi = argc
if_->R.rsi = argv 시작 주소
```

마지막에 `do_iret(&_if)`가 실행되면, PintOS는 이 구조체에 담긴 상태를 복원하면서 user mode로 진입한다.

## 13. `pml4` 멤버가 없다는 에러는 무시해도 되는가

무시하면 안 된다. 다만 코드 문제가 아니라 빌드 위치 문제일 가능성이 높다.

`struct thread`의 `pml4` 필드는 `USERPROG` 매크로가 켜진 경우에만 존재한다.

```c
#ifdef USERPROG
uint64_t *pml4;
#endif
```

Project 2는 `pintos/userprog`에서 빌드해야 한다. 이 디렉터리의 `Make.vars`에는 다음 define이 들어 있다.

```text
DEFINES = -DUSERPROG -DFILESYS
```

반대로 `pintos/threads`에서 빌드하면 `USERPROG`가 켜지지 않으므로 `pml4`가 없는 구조체로 컴파일된다. 따라서 Project 2 코드는 다음 위치에서 빌드해야 한다.

```sh
cd pintos/userprog
make
```

## 14. 배열 상한 검사는 왜 `ASSERT`보다 실패 처리인가

`ASSERT(argc < MAX_ARGS)`처럼 쓰면 개발 중 sanity check에는 도움이 될 수 있다. 하지만 사용자 입력이 너무 길다는 이유로 커널 전체가 panic하면 안 된다.

인자가 너무 많으면 사용자 프로그램 실행 실패로 처리하는 편이 맞다.

```text
토큰을 얻는다.
저장하기 전에 argc가 MAX_ARGS인지 확인한다.
상한이면 file_name과 argv_tokens를 해제하고 -1을 반환한다.
상한이 아니면 argv_tokens[argc]에 저장한다.
```

중요한 점은 검사 타이밍이다. 배열에 저장한 뒤 검사하면 이미 배열 밖을 썼을 수 있다. 반드시 저장 전에 검사해야 한다.

## 15. 현재 확정한 Parsing 규칙

최종적으로 확정한 규칙은 다음과 같다.

```text
argv_tokens는 char * 배열이다.
argv_tokens에는 문자열 내용이 아니라 token 시작 주소를 저장한다.
argv_tokens는 palloc_get_page()로 할당한 1페이지에 저장한다.
MAX_ARGS는 실제 토큰 최대 개수다.
MAX_ARGS는 PGSIZE / sizeof(char *) - 1이다.
argv_tokens[argc] = NULL을 유지한다.
사용자 스택에는 argv_tokens[0..argc-1]만 문자열로 복사한다.
사용자 스택에도 argv[argc] = NULL이 되도록 null pointer를 별도로 push한다.
process_exec()는 parsing까지만 담당한다.
load()는 빈 스택 생성 이후 user stack과 register setting을 담당한다.
file_name은 process_create_initd()에서 받은 page copy이므로 strtok_r()로 수정해도 된다.
file_name과 argv_tokens는 load()가 argument stack 구성을 끝낸 뒤 palloc_free_page()로 해제한다.
load()에는 argv_tokens[0], &_if, argc, argv_tokens를 넘긴다.
Project 2는 pintos/userprog에서 빌드해야 USERPROG와 pml4가 활성화된다.
```
