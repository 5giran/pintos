# PintOS Project 1 협업 규칙

이 문서는 브랜치 전략, commit 형식, PR 규칙, merge gate, 리뷰 방식을 정의한다. 전체 일정과 기능 순서는 `pintos/docs/plan.md`에서 관리한다.

## 1. 브랜치 전략

기본 브랜치:

```text
main
└── develop
    ├── feature/p1-alarm-clock
    ├── feature/p1-priority-scheduling
    ├── feature/p1-priority-donation
    ├── feature/p1-mlfqs
    ├── fix/p1-<test-or-bug-name>
    └── docs/p1-<topic>
```

브랜치 역할:

| 브랜치 | 역할 |
| --- | --- |
| `main` | 제출/발표 가능한 안정 브랜치 |
| `develop` | 기능 통합 브랜치 |
| `feature/p1-alarm-clock` | Alarm Clock 전체 구현 |
| `feature/p1-priority-scheduling` | donation 없는 priority scheduling 구현 |
| `feature/p1-priority-donation` | lock 기반 priority donation 구현 |
| `feature/p1-mlfqs` | MLFQS / Advanced Scheduler 구현 |
| `fix/p1-*` | merge 이후 발견된 실패 테스트 또는 버그 하나 수정 |
| `docs/p1-*` | 테스트 로그, 회고, 발표 메모 등 문서 작업 |

브랜치 규칙:

- 모든 feature 브랜치는 `develop`에서 만든다.
- `main`에 직접 push하지 않는다.
- `develop`에 직접 push하지 않는다.
- 기능 단위는 branch로 나누고, 같은 기능 안의 작은 단계는 commit으로 나눈다.
- Alarm처럼 선행 코드 위에서 이어지는 테스트 그룹은 하나의 feature branch에서 commit 단위로 관리한다.
- 테스트 케이스 기준으로 commit을 나누더라도 코드에서 테스트 이름을 보고 분기하지 않는다.

브랜치 생성 예시:

```bash
git checkout develop
git pull --rebase origin develop
git checkout -b feature/p1-alarm-clock
```

작업 중 develop 반영:

```bash
git checkout develop
git pull --rebase origin develop
git checkout feature/p1-alarm-clock
git rebase develop
```

충돌이 어렵거나 shared file 충돌이면 혼자 해결하지 말고 최소 1명과 함께 본다.

## 2. Commit 규칙

형식:

```text
<type>(<scope>): <summary>
```

type:

| type | 의미 |
| --- | --- |
| `feat` | 기능 구현 |
| `fix` | 버그 수정 |
| `refactor` | 동작 변경 없는 구조 개선 |
| `test` | 테스트 로그, 검증 스크립트, 테스트 관련 변경 |
| `docs` | 문서 |
| `chore` | 빌드, 설정, 기타 관리 작업 |
| `revert` | 되돌리기 |

scope 예시:

```text
timer
thread
synch
mlfqs
project1
docs
```

commit 예시:

```text
docs(project1): write alarm clock pseudocode and invariants
feat(timer): handle non-positive alarm sleep requests
feat(timer): add blocked sleep list wake-up path
feat(thread): schedule ready threads by priority
feat(synch): wake highest-priority semaphore waiters
feat(thread): separate base and effective priority
fix(thread): restore priority after lock release
feat(mlfqs): update load average and recent cpu
test(project1): record alarm clock test results
docs(project1): add donation troubleshooting note
```

commit 단위 원칙:

- 한 commit은 하나의 의미 있는 변경만 담는다.
- 기능 branch 안에서는 테스트 그룹 또는 논리 단위로 commit을 나눈다.
- debug용 `printf`가 들어간 commit은 PR 전 제거하거나 squash 대상에 포함한다.
- commit 메시지가 완벽하지 않아도 되지만, PR 제목과 설명은 반드시 명확해야 한다.
- 구현 전 의사코드나 invariant를 문서에 남기는 commit을 허용한다.

Alarm Clock 권장 commit 흐름:

```text
docs(project1): write alarm clock pseudocode and invariants
feat(timer): handle non-positive alarm sleep requests
feat(timer): add blocked sleep list wake-up path
test(project1): record alarm clock test results
```

Priority Scheduling 권장 commit 흐름:

```text
feat(thread): schedule ready threads by priority
feat(thread): yield when current priority is no longer highest
feat(synch): wake highest-priority semaphore and condition waiters
test(project1): record priority scheduling test results
```

Priority Donation 권장 commit 흐름:

```text
feat(thread): separate base and effective priority
feat(synch): donate priority through lock holders
feat(synch): propagate nested lock donation
fix(thread): restore effective priority after lock release
test(project1): record donation test results
```

MLFQS 권장 commit 흐름:

```text
feat(mlfqs): add fixed-point helpers
feat(mlfqs): update load average and recent cpu
feat(mlfqs): recalculate priority and nice values
test(project1): record mlfqs test results
```

## 3. PR 규칙

PR 제목 형식:

```text
<type>(project1): <summary>
```

PR 제목 예시:

```text
feat(project1): implement alarm clock sleep list
feat(project1): add priority scheduling
feat(project1): implement priority donation
feat(project1): implement mlfqs scheduler
fix(project1): restore priority after lock release
docs(project1): add week 1 plan and collaboration rules
```

PR 크기:

- 1 PR은 하나의 기능 또는 하나의 bug fix만 포함한다.
- Alarm, Priority Scheduling, Priority Donation, MLFQS를 한 PR에 섞지 않는다.
- 같은 기능 안의 테스트 그룹은 commit과 PR 본문에서 나눠 설명한다.
- 너무 큰 PR이 되면 기능을 다시 나누되, 선행 코드 의존성이 강한 테스트 그룹은 억지로 branch를 쪼개지 않는다.

PR 본문 템플릿:

````md
## 작업 요약

-

## 목표 테스트

- [ ] `make tests/threads/<test-name>.result`

## 구현 전 의사코드

-

## 유지해야 할 invariant

-

## 구현 내용

-

## 테스트 결과

```bash
cd pintos/threads/build
make tests/threads/<test-name>.result
```

```text
paste result here
```

## 리뷰 질문

-

## 위험 요소

-

## 학습 메모

-

## 체크리스트

- [ ] build 성공
- [ ] 목표 테스트 통과
- [ ] 관련 회귀 테스트 확인
- [ ] debug print 제거
- [ ] busy waiting 없음
- [ ] interrupt off 구간 최소화
- [ ] 테스트 이름 기반 하드코딩 없음
- [ ] 복잡한 invariant는 주석 또는 문서에 설명
````

## 4. 리뷰 방식

고정 Reviewer 역할은 두지 않는다. PR 작성자를 제외한 두 명이 함께 검토한다.

역할:

| 구분 | 책임 |
| --- | --- |
| Owner | 구현, PR 설명, 테스트 결과 기록, 리뷰 반영 |
| Correctness Checker | 요구사항, invariant, race condition, interrupt 관리 검토 |
| Test Checker | 목표 테스트, 회귀 테스트, 실패 로그 검토 |
| Merge Captain | merge gate 충족 여부 확인 후 merge |

2인 페어 프로그래밍 시 역할:

| 역할 | 책임 |
| --- | --- |
| Driver | 키보드를 잡고 의사코드와 코드를 작성한다. |
| Navigator / Scribe | 계속 질문을 던지고 invariant와 테스트 로그를 문서화한다. |
| Reviewer | PR opened 이후 changed files와 테스트 결과를 검토한다. |

Navigator가 던질 질문:

- 이 함수는 interrupt context에서 호출될 수 있는가?
- 여기서 thread가 block될 수 있는가?
- 이 list element는 동시에 다른 list에 들어갈 수 있는가?
- interrupt를 끈 구간이 꼭 필요한 범위인가?
- 이 변경이 다른 기능까지 건드리고 있지 않은가?
- 같은 priority나 같은 wake-up tick에서 순서가 설명 가능한가?
- busy waiting이 남아 있지 않은가?

리뷰 코멘트 형식:

```text
[관찰] 현재 코드는 A처럼 보입니다.
[위험] B 상황에서 C 문제가 생길 수 있습니다.
[질문/제안] D 방식으로 확인하거나 수정하는 것은 어떨까요?
```

## 5. Merge 규칙

기본 merge 방식은 Squash and Merge다.

Squash and Merge를 쓰는 이유:

- `develop` 기록을 기능 단위로 유지할 수 있다.
- 작업 중 생긴 WIP commit이 develop에 그대로 남지 않는다.
- 실패가 생겼을 때 PR 단위로 되돌리기 쉽다.
- 발표나 회고 때 어떤 기능이 언제 들어갔는지 보기 쉽다.

Squash commit 메시지:

```text
feat(project1): implement alarm clock sleep list
```

본문 예시:

```text
- Add wake-up tick tracking for sleeping threads
- Wake expired sleepers from timer interrupt
- Record alarm test results
```

develop merge gate:

- build 성공
- 목표 테스트 통과
- 관련 회귀 테스트 확인
- PR 작성자를 제외한 2명 확인
- debug print 없음
- 테스트 이름 기반 하드코딩 없음
- 실패 테스트가 있으면 PR 본문에 명시

main merge gate:

- `threads/build`에서 `make check` 성공
- PR 작성자를 제외한 2명 확인
- test log 최신화
- 발표/제출용 문서 최신화
- debug print 없음
- 임시 TODO 정리
- 팀원 3명이 핵심 구현 설명 가능

## 6. 코드 컨벤션

- 기존 PintOS 코드 스타일을 우선한다.
- 주변 코드와 indentation, naming, comment style을 맞춘다.
- kernel code에서는 floating-point 연산을 사용하지 않는다.
- file-local helper는 `static`으로 선언한다.
- 여러 파일에서 호출해야 하는 non-static 함수는 header에 prototype을 둔다.
- 복잡한 불변조건은 코드 근처 주석 또는 문서에 남긴다.
- 큰 배열이나 큰 구조체를 kernel stack에 올리지 않는다.

## 7. 테스트 로그 규칙

테스트 실패는 `docs/p1-test-log.md`에 남긴다.

템플릿:

```md
## <test-name>

- 증상:
- 재현 명령:
- 관련 파일/함수:
- 원인 가설:
- 실제 원인:
- 수정 내용:
- 재발 방지 체크:
- 배운 점:
```

테스트 로그 원칙:

- 통과 로그만 남기지 않는다.
- 실패한 순간의 증상과 가설을 남긴다.
- 테스트 이름 기반 하드코딩을 의심할 수 있는 변경은 기록하고 리뷰한다.
- race condition 의심은 재현 명령과 관찰 결과를 함께 적는다.
