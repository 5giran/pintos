# Pintos 디버그 printf 매크로 사용법

## 목적

기본 테스트 실행에서는 로그가 나오지 않고, 디버깅용 빌드 플래그를 켰을 때만 `printf` 로그가 찍히도록 한다.

추가한 헤더:

```c
#include <debug_log.h>
```

## 사용 예시

로그를 찍고 싶은 `.c` 파일에 헤더를 추가한다.

```c
#include <debug_log.h>
```

그 다음 원하는 위치에 매크로를 둔다.

```c
DBG ("child=%s tid=%d\n", thread_name (), tid);
DBG ("syscall=%d\n", syscall_nr);
DBG ("fault addr=%p\n", addr);
```

`DEBUG_LOG`가 켜져 있으면 `DBG(...)`는 바로 `printf(...)`로 연결된다. 기본 빌드에서는 `DBG(...)`가 `((void) 0)`으로 사라지므로 테스트 출력에 영향을 주지 않는다.

## 디버그 로그 켜기

디버그 로그를 켜려면 `DEFINES`에 `-DDEBUG_LOG`를 넘긴다.

```bash
cd /workspaces/pintos/pintos/vm
make p2-fork-one P2_FORK_TEST=fork-read DEFINES=-DDEBUG_LOG
```

출력 파일 확인:

```bash
cat build/tests/userprog/fork-read.output
```

출력 매크로는 `DBG(...)` 하나만 쓴다. 켜고 끄는 스위치도 `DEBUG_LOG` 하나만 쓴다.

## 주의사항

- `DEFINES`를 바꾼 뒤 기존 object 파일이 남아 있으면 변경된 플래그가 일부 파일에 반영되지 않을 수 있다.
- 로그가 안 찍히면 먼저 `make clean` 후 다시 빌드한다.
- 로그는 Pintos 테스트의 `.output` 파일에 섞이므로, 제출/기본 테스트 전에는 디버그 플래그 없이 다시 실행한다.
