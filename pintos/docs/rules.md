# PintOS Project 1, 2 협업 규칙

이 문서는 Project 1, 2 협업의 기준 문서다. `README.md`와 Notion에서 이 문서를 링크해 같은 기준을 보도록 유지한다. 전체 일정과 기능 순서는 `pintos/docs/plan.md`에서 관리한다.

## 1. 브랜치 전략

기본 구조:

```text
main
└── develop
    ├── feature/p1-alarm-clock
    ├── feature/p1-priority-scheduling
    ├── feature/p1-priority-donation
    ├── feature/p1-mlfqs
    ├── fix/p1-<test-or-bug-name>
    ├── compare/p1-<test-or-bug-name>
    └── <member>/<topic>
```

브랜치 역할:

| 브랜치 | 역할 |
| --- | --- |
| `main` | 최종 코드만 올리는 안정 브랜치 |
| `develop` | 테스트 통과 코드를 통합하고 리뷰하는 브랜치 |
| 하위 브랜치 | 개인 작업 브랜치. 새 기능 구현, 코드 수정, 주석 추가 작업은 여기서 진행 |

브랜치별 운영 원칙:

- `main`에는 최종 코드만 올린다.
- `main`으로 가는 변경은 피어 리뷰와 AI 리뷰를 거친 뒤, 필요한 리팩토링과 주석 정리까지 끝난 상태여야 한다.
- `develop`은 테스트 케이스를 통과한 변경을 올리는 브랜치다.
- `develop` PR은 목표 테스트가 통과했을 때만 올린다.
- `develop` PR 단계에서는 완벽한 주석 정리나 최종 리팩토링까지 요구하지 않는다.
- 하위 브랜치는 개인별로 생성해서 사용한다.
- 브랜치 이름은 작업 목적이 드러나게 짓고, 팀 공통 작업은 `feature/`, `fix/`, `compare/` 접두어를 권장한다.

브랜치 규칙:

- 모든 하위 브랜치는 `develop`에서 만든다.
- `main`에 직접 push하지 않는다.
- `develop`에 직접 push하지 않는다.
- 기능 단위는 branch로 나누고, 같은 기능 안의 작은 단계는 commit으로 나눈다.
- 선행 코드 위에서 이어지는 테스트 그룹은 하나의 feature branch에서 commit 단위로 관리한다.
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

## 2. Issue 규칙

Issue는 작업을 시작하기 전에 목표, 범위, 완료 기준을 팀원이 같은 기준으로 이해하기 위해 작성한다. 구현 중 발견한 bug, 추가 확인이 필요한 요구사항, 리팩토링 작업도 별도 issue로 남긴다.

Issue 제목 형식:

```text
<type>(<scope>): <summary>
```

type은 commit 규칙의 type을 그대로 사용한다.

Issue 제목 예시:

```text
feat(timer): implement alarm clock without busy waiting
feat(thread): add priority donation for nested locks
fix(synch): wake highest-priority waiter first
refactor(thread): split priority recalculation helper
docs(project1): organize mlfqs debugging notes
```

Issue 작성 원칙:

- 하나의 issue는 하나의 기능, 하나의 bug, 하나의 조사 주제만 다룬다.
- 구현 범위와 제외 범위를 함께 적는다.
- 관련 reference, 테스트 파일, 실패 로그가 있으면 링크나 경로를 남긴다.
- 목표 테스트와 회귀 테스트 후보를 구분해서 적는다.
- 작업 전 가설은 단정하지 않고 "확인할 것"으로 적는다.
- 구현 방향을 논의할 수는 있지만, 테스트 이름 기반 하드코딩이나 임시 우회는 완료 기준으로 삼지 않는다.
- issue가 커지면 선행 issue와 후속 issue로 나누고 의존 관계를 본문에 적는다.
- issue를 닫기 전에 연결된 PR, 통과한 테스트, 남은 위험 요소를 확인한다.

Issue 본문 템플릿:

````md
## 배경

-

## 작업 범위

-

## 제외 범위

-

## 기준 자료

- `pintos/docs/reference/...`
- `pintos/tests/...`

## 목표 테스트

- [ ] `make tests/<path>/<test-name>.result`

## 회귀 테스트 후보

- [ ] `make tests/<path>/<test-name>.result`

## 확인할 것

-

## 완료 기준

- [ ] 구현 범위가 코드 또는 문서에 반영됨
- [ ] 목표 테스트 통과 여부를 확인함
- [ ] 관련 회귀 테스트 영향 여부를 확인함
- [ ] debug print와 임시 코드가 남아 있지 않음
- [ ] PR 본문에 issue 번호를 연결함

## 위험 요소

-
````

## 3. Commit 규칙

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
| `test` | 테스트 관련 변경 |
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
test(project1): verify target thread tests
docs(project1): add donation troubleshooting note
```

commit 단위 원칙:

- 한 commit은 하나의 의미 있는 변경만 담는다.
- 기능 branch 안에서는 테스트 그룹 또는 논리 단위로 commit을 나눈다.
- 새 기능 구현, 코드 수정, 주석 추가가 있으면 해당 변경은 branch에 commit으로 남긴다.
- debug용 `printf`가 들어간 commit은 PR 전 제거하거나 squash 대상에 포함한다.
- commit 메시지가 완벽하지 않아도 되지만, PR 제목과 설명은 반드시 명확해야 한다.
- 구현 전 의사코드나 invariant를 문서에 남기는 commit을 허용한다.
- commit은 "언제 올릴지"보다 "무슨 변경을 묶는지"가 더 중요하다. 테스트 이름 하나보다 코드상 의미 단위를 우선한다.
- 테스트 통과 여부만 적는 commit보다, 왜 그 변경이 필요한지 드러나는 summary를 우선한다.

## 4. PR 규칙

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
- 서로 다른 목적의 구현이나 독립적인 수정은 한 PR에 섞지 않는다.
- 같은 기능 안의 테스트 그룹은 commit과 PR 본문에서 나눠 설명한다.
- 너무 큰 PR이 되면 기능을 다시 나누되, 선행 코드 의존성이 강한 변경은 억지로 branch를 쪼개지 않는다.

PR 올리는 기준:

- `develop`으로 보내는 PR은 목표 테스트가 통과했을 때만 연다.
- `develop` PR 단계에서는 완벽한 주석, 함수 분리, 최종 리팩토링까지 완료되어 있을 필요는 없다.
- `develop`에 merge된 뒤 1차 회귀 테스트를 진행한다.
- 1차 회귀 테스트 이후 함수 분리, 수동 리팩토링, 주석 추가를 진행한다.
- 그다음 2차 회귀 테스트를 진행한다.
- 2차 회귀 테스트 이후 AI PR review를 진행한다.
- `main`으로 보내는 PR은 위 과정을 모두 거친 최종 상태만 대상으로 한다.

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

## 5. 리뷰 방식

PR 작성자를 제외한 두 명이 함께 검토한다.

리뷰 흐름:

1. 개인 하위 브랜치에서 구현하고 commit을 쌓는다.
2. 목표 테스트 통과 후 `develop` PR을 연다.
3. 구현한 부분을 먼저 피어 코드 리뷰한 뒤 merge한다.
4. `develop` 기준 1차 회귀 테스트를 진행한다.
5. 함수 분리, 수동 리팩토링, 주석 보강을 진행한다.
6. 2차 회귀 테스트를 진행한다.
7. 이후 AI PR review를 진행한다.
8. 최종 정리된 코드만 `main`으로 올린다.

리뷰에서 기본적으로 확인할 질문:

- 이 함수는 interrupt context에서 호출될 수 있는가?
- 여기서 thread가 block될 수 있는가?
- 이 list element는 동시에 다른 list에 들어갈 수 있는가?
- interrupt를 끈 구간이 꼭 필요한 범위인가?
- 이 변경이 다른 기능까지 건드리고 있지 않은가?
- 같은 priority나 같은 wake-up tick에서 순서가 설명 가능한가?
- busy waiting이 남아 있지 않은가?
- 기존 함수 책임이 너무 커졌다면 분리하는 편이 더 명확하지 않은가?
- recovery path와 error path에서도 invariant가 유지되는가?

리뷰 코멘트 형식:

```text
[관찰] 현재 코드는 A처럼 보입니다.
[위험] B 상황에서 C 문제가 생길 수 있습니다.
[질문/제안] D 방식으로 확인하거나 수정하는 것은 어떨까요?
```

## 6. 코드 컨벤션

- 기존 PintOS 코드 스타일을 우선한다.
- 새 함수 이름과 코드 구조는 현재 `develop`에 올라간 주변 코드 스타일을 먼저 따른다.
- 주변 코드와 indentation, naming, comment style을 맞춘다.
- 함수 네이밍은 역할이 드러나게 짓고, 기존 모듈 prefix와 표현 방식을 유지한다.
- 하나의 함수는 가능한 한 하나의 핵심 책임에 집중하게 유지한다.
- 조건 분기가 많아져 흐름이 읽기 어려워지면 helper 함수로 나누는 것을 우선 검토한다.
- file-local helper는 `static`으로 선언한다.
- 여러 파일에서 호출해야 하는 non-static 함수는 header에 prototype을 둔다.
- header에 추가한 선언은 실제로 여러 곳에서 쓰이는지 확인하고, 불필요한 공개 선언을 늘리지 않는다.
- 구조체 필드를 추가할 때는 ownership과 갱신 시점을 함께 설명할 수 있어야 한다.
- 복잡한 불변조건은 코드 근처 주석 또는 문서에 남긴다.
- 주석은 코드가 "무엇을 하는지"보다 "왜 여기서 이 처리가 필요한지"를 설명하는 데 사용한다.
- debug print, 임시 TODO, 실험용 분기문은 PR 전에 제거한다.
- kernel code에서는 floating-point 연산을 사용하지 않는다.
- 큰 배열이나 큰 구조체를 kernel stack에 올리지 않는다.
- 공유 자료구조를 만질 때는 어떤 lock 또는 interrupt level이 그 변경을 보호하는지 분명해야 한다.
- interrupt를 끄는 구간은 최소 범위로 유지하고, 왜 필요한지 설명할 수 있어야 한다.
- list 정렬 기준이나 비교 함수는 호출 지점마다 달라지지 않도록 일관되게 관리한다.
- 테스트를 통과시키기 위한 하드코딩 대신, 일반 규칙이 성립하는 방향으로 구현한다.
- 이미 있는 함수의 의미를 바꿀 정도의 수정이면 호출자 전반의 영향 범위를 함께 확인한다.
- 한 commit 안에서 style 정리와 동작 변경을 과하게 섞지 않는다. 리뷰어가 의미를 따라갈 수 있어야 한다.
- 코드에서 드러나지 않는 검증 포인트는 PR 본문, commit 메시지, 또는 짧은 주석으로 남긴다.
