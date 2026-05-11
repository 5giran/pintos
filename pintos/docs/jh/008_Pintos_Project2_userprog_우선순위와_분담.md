# Pintos Project2 userprog 우선순위와 분담 계획

이 문서는 Project 2 `userprog`를 4명이 나누어 진행할 때의 작업 우선순위와 담당 기준을 정리한다. 기준은 로컬 reference, 로컬 테스트 파일, 강사 메모다. Pintos 과제 구현 코드는 포함하지 않는다.

## 기준 자료

- `docs/reference/pintos-kaist-original/2_project2/0_introduction.md`
- `docs/reference/pintos-kaist-original/2_project2/1_argument_passing.md`
- `docs/reference/pintos-kaist-original/2_project2/2_user_memory.md`
- `docs/reference/pintos-kaist-original/2_project2/3_system_call.md`
- `docs/reference/pintos-kaist-original/2_project2/4_process_termination.md`
- `docs/reference/pintos-kaist-original/2_project2/5_deny_write.md`
- `docs/reference/pintos-kaist-original/2_project2/6_dup.md`
- `docs/daily/2026-05-01.md`
- `pintos/tests/userprog/Rubric.functionality`
- `pintos/tests/userprog/Rubric.robustness`
- `pintos/tests/userprog/no-vm/Rubric`
- `pintos/tests/filesys/base/Rubric`
- `pintos/tests/userprog/Make.tests`

## 한 줄 결론

계획은 `argument_passing`을 먼저 끝내고, `write(fd=1)`/`exit`/`process_wait`로 테스트 출력과 종료 흐름을 안정화한 뒤, syscall 공통부와 user memory 검증을 세우고, file syscall, `exec/wait`, `fork`, `rox`, `multi-oom` 순서로 간다.

`dup2` extra는 fd 구조 전체를 흔들 수 있으므로 기본 테스트가 안정된 뒤 별도 작업으로 둔다.

## 강사 메모 사실 확인

강사 메모의 핵심 내용은 로컬 reference와 현재 skeleton 기준으로 대부분 맞다.

| 메모 내용 | 판단 | 근거 |
|---|---|---|
| `argument_passing`이 초반에 어렵고 의존성이 크다 | 맞음 | reference가 `process_exec()`에서 command line 파싱과 user stack 구성을 요구한다. |
| `printf`류 출력 때문에 `write(fd=1)`을 빨리 해야 한다 | 맞음 | `write` syscall 문서에서 fd `1`은 console 출력이다. 테스트의 `msg()` 출력도 여기에 의존한다. |
| `process_wait()`가 비어 있으면 테스트가 이상하게 종료될 수 있다 | 맞음 | reference는 initial process가 끝날 때까지 Pintos가 종료되면 안 된다고 한다. 현재 skeleton의 `process_wait()`는 아직 구현 대상이다. |
| file system 자체를 깊게 구현할 필요는 없다 | 대체로 맞음 | Project 2는 file system 구현 과제가 아니다. 다만 `file.h`, `filesys.h` 수준의 syscall 연결과 동기화는 필요하다. |
| `fork/exec/wait`는 어렵고 동기화가 필요하다 | 맞음 | reference가 `wait` 구현을 다른 syscall보다 훨씬 많은 작업이 필요하다고 설명한다. |
| `multi-oom`은 해석과 cleanup이 중요하다 | 맞음 | no-vm rubric은 `multi-oom` 하나가 담당하고, 테스트는 반복 fork, 비정상 종료, fd/resource cleanup을 함께 본다. |
| `dup2` extra는 기존 fd 구조를 망가뜨릴 수 있다 | 맞음 | extra 요구사항은 stdin/stdout close, fd alias, shared file offset, fork 이후 의미 보존까지 요구한다. |

보정할 점은 fd 설명이다. 일반 OS에서는 fd `0`, `1`, `2`를 흔히 말하지만, Project 2 reference에서 기본으로 명시된 것은 fd `0` stdin, fd `1` stdout이다. fd `2`는 기본 요구사항의 중심으로 보지 않는다.

## 전체 우선순위

| 우선순위 | 기능 단위 | 먼저 해야 하는 이유 | 통과 목표 |
|---:|---|---|---|
| 0 | Project 1 안정성 확인 | Project 2는 Project 1 위에 쌓인다. thread/sync 버그가 있으면 userprog에서 랜덤 실패처럼 보일 수 있다. | threads 주요 테스트가 비정상적으로 흔들리지 않는지 확인 |
| 1 | `argument_passing` | 모든 user program 실행의 첫 관문이다. command line 파싱과 user stack layout이 틀리면 이후 syscall 테스트도 의미 있게 보기 어렵다. | `args-none`, `args-single`, `args-multiple`, `args-many`, `args-dbl-space` |
| 2 | `write(fd=1)`, `exit`, 최소 `process_wait` | 테스트 출력과 종료 흐름을 안정화한다. 출력 syscall이 없으면 실패 원인 확인도 어려워진다. | `halt`, `exit`, `args-*` 출력 안정화 |
| 3 | `syscall_entry`와 dispatch | syscall 번호와 인자를 꺼내서 기능별 handler로 보내는 공통 기반이다. | 모든 syscall 구현의 진입부 |
| 4 | `user_memory_access` | invalid pointer, kernel address, page boundary buffer가 kernel panic을 만들지 않게 해야 한다. | `*-bad-ptr`, `*-boundary`, `bad-read`, `bad-write`, `bad-jump` 계열 |
| 5 | `file_syscall`과 fd table | 점수 효율이 크고, functionality/robustness/filesys base까지 연결된다. | `create-*`, `open-*`, `read-*`, `write-*`, `close-*`, fd robustness, filesys base 일부 |
| 6 | `filesize`, `remove`, `seek`, `tell` | userprog 보조 함수와 filesys/base 테스트가 사용한다. 기본 file syscall과 함께 끝내야 한다. | `read-normal`, `multi-child-fd`, `filesys/base` 계열 |
| 7 | `exec`와 `wait` | `fork`보다 먼저 load 성공/실패, exit status, parent-child record를 안정화하는 편이 좋다. | `exec-once`, `exec-arg`, `exec-missing`, `exec-read`, `wait-simple`, `wait-twice`, `wait-killed`, `wait-bad-pid` |
| 8 | `fork`와 fd/address space 복제 | 가장 복잡한 핵심 기능이다. parent-child 동기화, fd 복제, 주소공간 복제가 모두 걸린다. | `fork-once`, `fork-multiple`, `fork-close`, `fork-read`, `fork-recursive`, `multi-recurse`, `multi-child-fd` |
| 9 | executable write deny | 실행 중인 executable write 금지다. process load/exit 흐름이 잡힌 뒤 붙이는 것이 안전하다. | `rox-simple`, `rox-child`, `rox-multichild` |
| 10 | cleanup과 stress | fd, child record, page, abnormal exit cleanup이 모두 걸린다. 마지막에만 보지 말고 7~8단계부터 계속 확인한다. | `multi-oom`, `wait-killed` |
| 11 | `dup2` extra | 기본 fd 설계를 바꾸거나 alias 구조를 추가해야 할 수 있다. 기본 통과 후 진행한다. | `dup2-simple`, `dup2-complex` |

## 기능 단위 설명

| 기능 단위 | 핵심 내용 | 난이도 | 의존성 |
|---|---|---|---|
| `argument_passing` | command line을 token으로 나누고 user stack에 문자열, `argv[]`, null sentinel, fake return address, `argc/argv` register를 규칙대로 배치한다. | 중~상 | process load 흐름 이해 |
| `basic_syscall` | `halt`, `exit`, `write(fd=1)` 중심으로 최소 출력과 종료를 만든다. | 하~중 | syscall dispatch |
| `syscall_entry` | syscall 번호와 인자를 register에서 읽고 syscall별 함수로 분기한다. return value는 `rax`에 반영한다. | 중 | x86-64 syscall calling convention |
| `user_memory_access` | user pointer, string, buffer 범위를 검증한다. invalid pointer는 kernel panic이 아니라 process 종료나 error path로 처리한다. | 상 | page table, user/kernel address 구분 |
| `file_syscall` | `create`, `open`, `read`, `write`, `close`, `filesize`, `remove`, `seek`, `tell`을 fd table과 연결한다. | 중 | user memory helper, file system lock |
| `process_syscall` | `exec`, `wait`, `fork`의 parent-child 관계, load 결과 전달, exit status 회수, 중복 wait 방지를 처리한다. | 상 | synchronization, child record lifecycle |
| `fd_inheritance` | `fork` 이후 child가 fd를 사용할 수 있어야 하고, child의 close가 parent fd를 깨뜨리면 안 된다. `exec` 후에도 fd는 유지된다. | 상 | fd table 복제 정책 |
| `deny_write` | 실행 중인 executable file에 대한 write를 막고, process 종료 시 정상 해제한다. | 중 | executable file handle 보관 |
| `cleanup` | 정상/비정상 종료 시 fd, executable, child record, page/resource를 정리한다. | 상 | exit path 일원화 |
| `dup2_extra` | fd alias, target fd close, shared file offset, stdin/stdout close semantics를 처리한다. | 상 | fd table 재설계 가능성 |

## 4인 분담안

개발 경험 있는 2명은 공통 contract와 lifecycle을 맡고, 경험 적은 2명은 테스트 단위가 비교적 분명한 기능을 맡는다. 단, `argument_passing`은 초반 난관이므로 경험 적은 사람에게 단독으로 맡기지 않고 경험자가 설계/리뷰를 같이 한다.

| 담당 | 경험 기준 | 주 담당 | 보조 담당 | 주요 테스트 |
|---|---|---|---|---|
| A | 개발 경험 있음 | `syscall_entry`, `user_memory_access`, invalid pointer 정책, 전체 syscall contract | `argument_passing` 리뷰 | `*-bad-ptr`, `*-boundary`, `bad-*` |
| B | 개발 경험 있음 | `process_wait`, `exec`, `wait`, `fork`, fd/address space 복제, cleanup | `multi-oom`, `rox` 리뷰 | `exec-*`, `wait-*`, `fork-*`, `multi-recurse`, `multi-oom` |
| C | 경험 적음 | `argument_passing`, `halt`, `exit`, `write(fd=1)` | 종료 메시지 확인 | `args-*`, `halt`, `exit` |
| D | 경험 적음 | `file_syscall`, fd table 기본 동작 | filesys/base 확인 | `create-*`, `open-*`, `read-*`, `write-*`, `close-*`, `filesize/remove/seek/tell`, `filesys/base` |

## 담당자별 작업 묶음

이 표는 "각자 지금 무엇부터 잡아야 하는가"를 기준으로 다시 묶은 것이다. A/B/C/D는 실제 팀원 이름으로 바꿔 쓰면 된다.

### A: syscall 공통부와 pointer 안정성 담당

개발 경험 있는 사람이 맡는다. 다른 사람이 만든 syscall이 모두 이 contract 위에서 돌아가므로 초반 우선순위가 가장 높다.

| 개인 우선순위 | 맡을 기능 | 완료 기준 |
|---:|---|---|
| A-1 | `syscall_entry`와 dispatch 구조 | syscall 번호를 읽고 syscall별 handler로 분기할 수 있다. |
| A-2 | syscall 인자 읽기 규칙 | register 인자 순서와 return value 처리 기준이 정리된다. |
| A-3 | `user_memory_access` 공통 helper 정책 | user pointer, string, buffer 검증 기준이 하나로 정해진다. |
| A-4 | bad pointer와 boundary 계열 방어 | `*-bad-ptr`, `*-boundary`, `bad-read/write/jump*`가 kernel panic 없이 처리된다. |
| A-5 | 다른 담당자 syscall 리뷰 | file/process syscall이 pointer 검증 helper를 일관되게 쓰는지 확인한다. |

### B: process lifecycle 담당

개발 경험 있는 사람이 맡는다. 가장 복잡하고, cleanup과 동기화 문제가 많이 생기는 영역이다.

| 개인 우선순위 | 맡을 기능 | 완료 기준 |
|---:|---|---|
| B-1 | 최소 `process_wait` | initial process가 child 종료 전에 Pintos를 끝내지 않는다. |
| B-2 | `exec` load 성공/실패 처리 | `exec-once`, `exec-arg`, `exec-missing`, `exec-read` 흐름이 맞는다. |
| B-3 | `wait`와 child record | `wait-simple`, `wait-twice`, `wait-bad-pid`, `wait-killed` 기준을 만족한다. |
| B-4 | `fork` 기본 동작 | parent/child return value, 주소공간 복제, fork 성공/실패 동기화가 맞는다. |
| B-5 | fd 상속/복제와 cleanup | `fork-read`, `fork-close`, `multi-child-fd`, `multi-recurse`, `multi-oom`을 본다. |

### C: 실행 시작과 기본 출력 담당

경험이 적은 사람이 맡되, `argument_passing`은 초반 난관이므로 A가 설계와 리뷰를 같이 한다.

| 개인 우선순위 | 맡을 기능 | 완료 기준 |
|---:|---|---|
| C-1 | `argument_passing` 이해와 테스트 추적 | `args.c`, expected output, stack layout 요구사항을 설명할 수 있다. |
| C-2 | `argument_passing` 구현 범위 담당 | `args-none`, `args-single`, `args-multiple`, `args-many`, `args-dbl-space`가 목표다. |
| C-3 | `halt`, `exit` | 기본 종료 syscall과 종료 메시지 형식이 맞는다. |
| C-4 | `write(fd=1)` | 테스트의 `msg()`/출력이 안정적으로 보인다. |
| C-5 | 종료 메시지 회귀 확인 | 다른 기능이 붙은 뒤에도 output checker가 깨지지 않도록 불필요한 출력이 없는지 본다. |

### D: file syscall과 fd table 담당

경험이 적은 사람이 맡기 좋지만, fd table 구조는 A/B와 먼저 맞춘다. file syscall은 테스트가 기능별로 잘 나뉘어 있어 병렬 작업에 적합하다.

| 개인 우선순위 | 맡을 기능 | 완료 기준 |
|---:|---|---|
| D-1 | fd table 기본 구조 | fd `0`/`1`과 일반 file fd의 처리 기준이 분리된다. |
| D-2 | `create`, `open`, `close` | `create-*`, `open-*`, `close-*` 기본/에러 테스트를 본다. |
| D-3 | `read`, `write` | file read/write와 stdin/stdout/bad fd 처리를 구분한다. |
| D-4 | `filesize`, `remove`, `seek`, `tell` | `tests/lib.c`의 file 검증과 `filesys/base`에서 필요한 syscall을 채운다. |
| D-5 | file system synchronization 확인 | concurrent file 접근에서 file system lock이 빠지지 않았는지 A/B와 확인한다. |

## 의존성 기반 병렬 작업 흐름

아래 표는 실제 작업 상태를 세 가지로 나눈다.

- `진행 가능`: 지금 바로 구현/설계에 들어가도 된다.
- `부분 진행`: 문서/테스트 분석, 인터페이스 설계는 가능하지만 구현 완료는 blocker가 풀려야 한다.
- `대기`: 선행 작업 없이는 구현하면 충돌하거나 다시 갈아엎을 가능성이 크다.

### 흐름 1: 공통 기반 열기

| 담당 | 상태 | 작업 | 막히는 지점 | 대기 중 할 일 | 풀리면 열리는 작업 |
|---|---|---|---|---|---|
| A | 진행 가능 | `syscall_entry`/dispatch 기준 잡기 | 없음 | syscall 번호, register 인자 순서, return value 규칙 정리 | C의 `halt/exit/write`, D의 file syscall 연결 |
| B | 진행 가능 | 최소 `process_wait` 방향 잡기 | 없음 | initial process 종료 흐름, child record 후보 정리 | user program 테스트가 조기 종료 없이 관찰 가능 |
| C | 부분 진행 | `argument_passing` 문서/테스트 분석 | `write(fd=1)`와 최소 `process_wait` 없이는 결과 확인이 불안정함 | stack layout, `args-*` expected output, 공백 처리 케이스 정리 | `argument_passing` 구현 검증 |
| D | 부분 진행 | fd table 요구사항 분석 | A의 dispatch와 pointer helper 정책이 없으면 syscall 연결 방식이 흔들림 | fd `0`/`1`, 일반 fd, invalid fd, fd 상속 요구사항 정리 | fd table과 `create/open/close` 구현 |

### 흐름 2: 출력/종료와 pointer contract 확정

| 담당 | 상태 | 작업 | 막히는 지점 | 대기 중 할 일 | 풀리면 열리는 작업 |
|---|---|---|---|---|---|
| A | 진행 가능 | `user_memory_access` 정책 확정 | dispatch 기준이 먼저 있어야 syscall에 일관 적용 가능 | string/buffer/page boundary 테스트 읽기 | D의 robust file syscall, B의 robust `exec/fork` |
| B | 부분 진행 | child record와 `wait` 설계 | 최소 `process_wait` 기준과 exit status 저장 위치가 정해져야 함 | `wait-simple`, `wait-twice`, `wait-bad-pid`, `wait-killed` expected output 확인 | `wait` 구현, 이후 `fork` |
| C | 진행 가능 | `halt`, `exit`, `write(fd=1)` | A의 dispatch가 필요함. dispatch가 끝나기 전에는 handler 내부 동작만 설계 가능 | 종료 메시지 형식, 불필요한 출력 금지 기준 확인 | `args-*` 출력 확인 |
| D | 부분 진행 | fd table 기본 구조 설계 | A의 pointer 정책, A/B와 fd policy 합의 필요 | `open-twice`, `close-twice`, `read-stdout`, `write-stdin` 테스트 분석 | `create/open/close` |

### 흐름 3: 첫 기능 통과 묶음 만들기

| 담당 | 상태 | 작업 | 막히는 지점 | 대기 중 할 일 | 풀리면 열리는 작업 |
|---|---|---|---|---|---|
| C | 진행 가능 | `argument_passing` 완료 목표 | `write(fd=1)`와 최소 `process_wait`가 있어야 통과 여부를 안정적으로 볼 수 있음 | 실패 시 stack dump/expected output 비교 절차 정리 | `exec-arg`, child program 인자 전달 확인 |
| D | 진행 가능 | `create/open/close` | A의 string pointer 검증 helper가 필요함 | file name null/empty/too long 케이스 정리 | `read/write/filesize` |
| A | 진행 가능 | bad pointer/boundary 1차 대응 | helper 정책 확정 후 각 syscall 적용은 D/B 코드가 있어야 확인 가능 | D/B가 helper를 일관되게 쓰는지 리뷰 | file/process robust 테스트 |
| B | 부분 진행 | `exec` load 성공/실패 | A의 string 검증, C의 argument passing 방향 필요 | `exec-missing`, `exec-read` 테스트 분석 | `wait`와 `fork` 연결 |

### 흐름 4: file syscall과 process wait 연결

| 담당 | 상태 | 작업 | 막히는 지점 | 대기 중 할 일 | 풀리면 열리는 작업 |
|---|---|---|---|---|---|
| D | 진행 가능 | `read/write/filesize` | A의 buffer 검증 helper가 필요함 | stdin/stdout/bad fd 처리 기준 확인 | `multi-child-fd`, `filesys/base` 일부 |
| B | 진행 가능 | `wait` 구현과 status 회수 | child record 기준과 `exec` 흐름 필요 | abnormal exit 시 status `-1` 기준 정리 | `fork` |
| A | 부분 진행 | syscall 사용부 pointer 리뷰 | D/B의 syscall 초안이 있어야 적용 누락을 볼 수 있음 | bad pointer 테스트 묶음별 실패 원인 표 작성 | robustness 안정화 |
| C | 부분 진행 | args/출력/종료 회귀 확인 | A/B/D의 변경이 들어와야 회귀 확인 가능 | output checker를 깨는 추가 출력 여부 확인 | 다른 담당자 테스트 확인 보조 |

### 흐름 5: fork, fd 상속, filesys base

| 담당 | 상태 | 작업 | 막히는 지점 | 대기 중 할 일 | 풀리면 열리는 작업 |
|---|---|---|---|---|---|
| B | 진행 가능 | `fork`와 address space/fd 복제 | `wait`와 D의 fd table 기본 동작이 필요함 | `fork-close`, `fork-read`, `multi-child-fd` 의미 재확인 | recursive/process stress 테스트 |
| D | 진행 가능 | `remove/seek/tell`, fd robustness | file syscall 기본 통과가 필요함 | `filesys/base` 테스트가 요구하는 syscall 확인 | `filesys/base`, `exec-read`, `multi-child-fd` |
| A | 부분 진행 | boundary와 exception robustness | B/D의 syscall 호출부가 있어야 실제 적용 확인 가능 | `bad-read/write/jump*` 계열 점검 | `multi-oom`에서 kernel panic 위험 감소 |
| C | 부분 진행 | `rox-*` 테스트 분석 또는 보조 | B의 exec/load 흐름과 executable handle 정책 필요 | `rox-*` expected output과 실행 파일 write 방지 요구 확인 | deny-write 작업 보조 |

### 흐름 6: cleanup과 stress 안정화

| 담당 | 상태 | 작업 | 막히는 지점 | 대기 중 할 일 | 풀리면 열리는 작업 |
|---|---|---|---|---|---|
| B | 진행 가능 | cleanup과 `multi-oom` | `fork`, `wait`, fd cleanup 흐름이 있어야 실제 검증 가능 | abnormal exit, child record 해제, fd 해제 체크리스트 작성 | no-vm 안정화 |
| A | 부분 진행 | 전체 robustness 회귀 | B/D의 cleanup 반영이 필요함 | pointer helper 누락 syscall 찾기 | 최종 userprog robustness 확인 |
| D | 부분 진행 | filesys/base 확인 | file syscall과 file system lock이 필요함 | sync read/write/remove 실패 원인 후보 정리 | base filesystem 15% 확보 |
| C | 부분 진행 | `rox-*` 보조 확인 | B의 executable handle 정책 필요 | 종료 메시지/출력 회귀 확인 | deny-write 안정화 |

### 흐름 7: extra 판단

| 담당 | 상태 | 작업 | 막히는 지점 | 대기 중 할 일 | 풀리면 열리는 작업 |
|---|---|---|---|---|---|
| B/A | 대기 | extra `dup2` 착수 | 기본 userprog, robustness, no-vm 안정화 전에는 fd 구조를 흔들 위험이 큼 | `dup2-simple`, `dup2-complex` 요구사항만 읽기 | extra 착수 또는 보류 |
| C/D | 대기 | extra 보조 | B/A의 fd alias 설계가 먼저 필요함 | stdin/stdout close와 shared offset 테스트 분석 | extra 테스트 확인 |

### 핵심 대기 관계

| 기다리는 작업 | 막히는 작업 | 이유 |
|---|---|---|
| A의 syscall dispatch | C의 `halt/exit/write`, D의 file syscall, B의 syscall 형태 `exec/wait/fork` | syscall handler가 분기하지 못하면 기능 handler를 호출할 수 없다. |
| C의 `write(fd=1)`와 B의 최소 `process_wait` | `args-*`의 안정적인 확인 | user program이 출력하고, main process가 child를 기다려야 expected output을 볼 수 있다. |
| A의 user pointer/string/buffer 검증 | D의 robust file syscall, B의 robust `exec/fork` | bad pointer와 boundary 테스트가 모든 syscall에 걸린다. |
| D의 fd table 기본 구조 | B의 fd 상속/복제, `exec-read`, `multi-child-fd` | process syscall은 fd 상태를 복제하거나 유지해야 한다. |
| B의 `wait`와 child record | B의 `fork`, `multi-recurse`, `multi-oom` | fork 이후 parent-child status 회수와 cleanup이 같은 자료구조에 의존한다. |
| B의 `exec/load` 흐름 | `rox-*`, `exec-read`, 일부 cleanup | executable file handle과 fd 유지 정책이 exec/load와 연결된다. |

## 단계별 진행 게이트

각 단계는 다음 단계로 넘어가기 전에 최소한의 테스트 묶음이 안정적으로 통과해야 한다.

| 단계 | 완료 기준 |
|---:|---|
| 1 | `args-*`가 출력 형식까지 맞게 통과한다. |
| 2 | `halt`, `exit`, `write(fd=1)` 기반 출력이 안정적이고 initial process가 조기 종료되지 않는다. |
| 3 | syscall dispatch가 syscall 번호별로 분기되고, 알 수 없는 syscall이나 invalid argument가 kernel panic으로 이어지지 않는다. |
| 4 | bad pointer, boundary, bad jump/read/write 계열이 kernel panic 없이 기대 결과를 낸다. |
| 5 | 기본 file syscall과 fd robustness가 통과한다. fd `0`/`1` 처리와 일반 file fd 처리가 분리되어 있다. |
| 6 | `exec` load 성공/실패와 `wait` status 회수가 안정적이다. `wait-twice`, `wait-bad-pid`, `wait-killed`를 확인한다. |
| 7 | `fork` 이후 parent/child가 독립적으로 실행되고, child return value와 parent return value가 구분된다. |
| 8 | fd 상속/복제 관련 테스트가 통과한다. child가 fd를 닫아도 parent fd가 살아 있어야 한다. |
| 9 | `rox-*`와 `multi-oom`까지 확인한다. 반복 실행해도 depth가 줄거나 resource leak 증상이 없어야 한다. |
| 10 | extra를 선택한 경우에만 `dup2-*`를 진행한다. |

## 주의할 해석 포인트

### fd inheritance

`fork`는 child에게 fd를 복제해야 한다. 따라서 `multi-child-fd`는 "fd 상속 금지" 테스트가 아니다. child가 상속받은 fd를 읽고 닫을 수 있어야 하며, 그 close가 parent의 fd를 망가뜨리면 안 된다.

`exec`도 fd를 닫지 않는다. reference는 `exec` 후에도 file descriptor가 열린 상태로 남는다고 설명한다.

### user pointer 검증

문자열 pointer는 첫 주소만 보면 부족하다. null terminator까지 안전하게 읽을 수 있어야 한다.

buffer pointer는 시작 주소만 보면 부족하다. `[buffer, buffer + size)` 범위가 user 영역에서 유효해야 한다. boundary 테스트는 이 지점을 노린다.

### file system lock

Project 2에서 file system 자체를 구현하지는 않는다. 그러나 제공 file system은 내부 동기화가 없으므로 syscall 쪽에서 file system 접근을 critical section으로 보호해야 한다.

`process_exec()`도 file system에 접근하므로 file syscall만 lock을 고려하면 부족할 수 있다.

### cleanup

cleanup은 마지막에 한 번에 붙이는 기능이 아니다. `exit`, `wait`, `exec`, `fork`, fd close, executable deny-write 해제가 서로 얽힌다. `multi-oom`은 이 설계가 누수 없이 동작하는지 반복적으로 확인하는 stress 테스트에 가깝다.

## 테스트 실행 참고

AI는 이 과제 구현 테스트를 직접 수행하지 않는다. 테스트가 필요하면 `TESTING.md`의 명령을 사용해 Docker 컨테이너 안에서 실행한다.

개별 테스트 예시는 다음 형식이다.

```bash
export PINTOS_ROOT=/workspace/pintos
source "$PINTOS_ROOT/activate"
make -C "$PINTOS_ROOT/userprog" build/tests/userprog/args-none.result
```

전체 userprog 테스트는 다음 형식이다.

```bash
make -C "$PINTOS_ROOT/userprog" clean
make -C "$PINTOS_ROOT/userprog" check
cat "$PINTOS_ROOT/userprog/build/results"
```

## 최종 운영 원칙

- 먼저 공통 contract를 합의한다: exit status, invalid pointer 처리, fd table 구조, child record lifecycle.
- 각자 맡은 기능은 해당 기능의 happy path만 보지 말고 rubric의 edge case까지 같이 본다.
- file syscall은 나눠서 구현하기 좋지만, fd table과 user memory helper는 공통 기반이므로 임의로 중복 설계하지 않는다.
- `fork/exec/wait`와 cleanup은 경험자가 책임지고, 다른 담당자의 fd/file syscall 결과와 계속 맞춘다.
- extra `dup2`는 기본 통과 후 선택한다. 기본 fd 구조를 바꾸는 작업이 될 수 있으므로 초반 병행은 피한다.
