# PintOS KAIST Project 1 - Threads 1주차 계획

- 대상: 크래프톤 정글 SW-AI 12기 3인 팀
- 과제: KAIST PintOS 64-bit Project 1: Threads
- 기간: 2026-04-25(토) ~ 2026-04-30(목)
- 발표 준비 마감: 2026-04-29(수) 저녁
- 공식 문서: https://casys-kaist.github.io/pintos-kaist/

## 1. 이번 주 목표

이번 주 목표는 KAIST PintOS 64-bit Project 1: Threads 테스트를 통과하는 것이다.

다만 팀의 주 목적은 학습이다. 따라서 테스트 통과만으로 끝내지 않고, 팀원 3명이 각 기능의 동작 원리, race condition 가능성, interrupt 관리 이유, scheduler invariant를 설명할 수 있어야 한다.

이번 주 구현 범위는 아래 네 기능이다.

| 기능 | 목표 | 주요 테스트 그룹 |
| --- | --- | --- |
| Alarm Clock | busy waiting 없이 thread를 timer tick 기준으로 재우고 깨운다. | `alarm-*` |
| Priority Scheduling | 높은 priority thread가 먼저 실행되도록 scheduler와 waiters를 정리한다. | `priority-preempt`, `priority-change`, `priority-fifo`, `priority-sema`, `priority-condvar`, `alarm-priority` |
| Priority Donation | lock 기반 priority inversion을 완화한다. | `priority-donate-*` |
| MLFQS / Advanced Scheduler | `-mlfqs` 옵션에서 4.4BSD 방식 scheduler를 구현한다. | `mlfqs-*` |

## 2. 산출물 구조

이번 문서 세트는 역할을 분리한다.

| 파일 | 역할 |
| --- | --- |
| `pintos/docs/plan.md` | 전체 주간 계획, 기능 순서, 일정, 산출물 목록 |
| `pintos/docs/rules.md` | 브랜치 전략, commit 규칙, PR 규칙, merge gate, 리뷰 방식 |
| `pintos/docs/p1_alarm_clock_today_plan.pdf` | Alarm Clock 구현 전 확인 파일, 함수, 의사코드, 테스트 계획 |
| `pintos/docs/p1_priority_scheduling_plan.pdf` | Priority Scheduling 구현 전 확인 파일, 함수, invariant, 테스트 계획 |
| `pintos/docs/p1_priority_donation_plan.pdf` | Priority Donation 구현 전 확인 파일, 함수, invariant, 테스트 계획 |
| `pintos/docs/p1_mlfqs_advanced_scheduler_plan.pdf` | MLFQS 구현 전 확인 파일, 함수, fixed-point/timing 확인 계획 |
| `pintos/docs/p1_week1_overall_plan_revised.pdf` | 전체 계획 PDF 요약본 |

## 3. 전체 진행 순서

기능 순서는 아래처럼 진행한다.

1. Alarm Clock
2. Priority Scheduling
3. Priority Donation
4. MLFQS / Advanced Scheduler
5. 전체 회귀 테스트와 발표 준비

이 순서를 따르는 이유:

- Alarm Clock은 Project 1의 가장 작은 독립 기능이고, busy waiting 제거와 interrupt 관리 학습에 적합하다.
- Priority Scheduling은 이후 `alarm-priority`, semaphore waiters, condition waiters에 영향을 준다.
- Priority Donation은 priority scheduler가 안정된 뒤 lock 관계를 다루는 것이 낫다.
- MLFQS는 donation과 함께 적용하지 않으므로 마지막에 별도 모드로 분리해 검증한다.

## 4. D0 - 2026-04-25(토): Alarm Clock

목표:

- Alarm Clock 기본 테스트 통과
- 2인 페어 프로그래밍으로 의사코드 작성 후 구현
- Navigator / Scribe가 질문과 문서화를 담당
- 나머지 1명은 PR review와 테스트 로그 확인 담당

작업 브랜치:

```text
feature/p1-alarm-clock
```

목표 테스트:

```text
alarm-zero
alarm-negative
alarm-single
alarm-multiple
alarm-simultaneous
```

후속 테스트:

```text
alarm-priority
```

`alarm-priority`는 priority scheduler와 연결되는 테스트이므로 Priority Scheduling 구현 후 회귀 테스트로 확인한다.

완료 기준:

- Alarm 기본 테스트 5개가 통과한다.
- `timer_sleep()`에 busy waiting이 남아 있지 않다.
- interrupt disable 구간의 이유를 설명할 수 있다.
- timer interrupt에서 block/sleep 가능한 함수를 호출하지 않는 이유를 설명할 수 있다.
- PR 본문에 의사코드, invariant, 테스트 결과, 남은 위험이 기록되어 있다.

## 5. D1 - 2026-04-26(일): Priority Scheduling

목표:

- ready list에서 높은 priority thread가 먼저 선택되게 한다.
- priority가 높은 thread가 ready 상태가 되었을 때 선점 조건을 정리한다.
- semaphore waiters와 condition waiters도 priority 기준으로 처리한다.
- donation 없이 통과 가능한 priority 테스트를 먼저 통과시킨다.

작업 브랜치:

```text
feature/p1-priority-scheduling
```

목표 테스트:

```text
priority-preempt
priority-change
priority-fifo
priority-sema
priority-condvar
alarm-priority
```

완료 기준:

- donation이 필요 없는 priority 테스트가 통과한다.
- Alarm Clock 테스트가 회귀하지 않는다.
- ready list와 waiters의 정렬 invariant를 설명할 수 있다.
- interrupt context에서 직접 yield하지 않는 이유를 설명할 수 있다.

## 6. D2 - 2026-04-27(월): Priority Donation

목표:

- lock 기반 priority donation을 구현한다.
- base priority와 effective priority를 분리한다.
- multiple donation, nested donation, lock release 후 priority 복구를 처리한다.
- semaphore 대기 중 lock donation edge case를 확인한다.

작업 브랜치:

```text
feature/p1-priority-donation
```

목표 테스트:

```text
priority-donate-one
priority-donate-multiple
priority-donate-multiple2
priority-donate-nest
priority-donate-chain
priority-donate-lower
priority-donate-sema
```

완료 기준:

- donation 관련 테스트가 통과한다.
- priority scheduling과 alarm 테스트가 회귀하지 않는다.
- lock release 시 어떤 donation을 제거해야 하는지 설명할 수 있다.
- nested donation이 필요한 상황을 설명할 수 있다.

## 7. D3 - 2026-04-28(화): MLFQS / Advanced Scheduler

목표:

- `-mlfqs` 옵션에서 advanced scheduler를 구현한다.
- fixed-point 연산을 사용한다.
- `load_avg`, `recent_cpu`, `nice`, priority 재계산 timing을 맞춘다.
- MLFQS 모드에서는 priority donation이 적용되지 않게 한다.

작업 브랜치:

```text
feature/p1-mlfqs
```

목표 테스트:

```text
mlfqs-load-1
mlfqs-load-60
mlfqs-load-avg
mlfqs-recent-1
mlfqs-fair-2
mlfqs-fair-20
mlfqs-nice-2
mlfqs-nice-10
mlfqs-block
```

완료 기준:

- MLFQS 테스트가 통과한다.
- non-MLFQS 테스트가 회귀하지 않는다.
- fixed-point 계산과 update timing을 설명할 수 있다.
- Donation과 MLFQS가 동시에 적용되지 않는 구조를 설명할 수 있다.

## 8. D4 - 2026-04-29(수): 회귀 테스트와 발표 준비

목표:

- `develop`에서 Project 1 전체 테스트를 실행한다.
- 실패 테스트가 있으면 `fix/p1-*` 브랜치로 분리한다.
- 테스트 로그와 발표 자료를 정리한다.
- 신규 기능 추가보다 안정화와 설명 가능성을 우선한다.

작업:

```bash
cd pintos/threads
make clean
make
cd build
make check
```

완료 기준:

- 전체 테스트 결과가 팀에 공유되어 있다.
- 실패가 있으면 원인 가설과 재현 명령이 기록되어 있다.
- 발표 자료 초안이 있다.
- 팀원 3명이 각자 최소 하나의 구현 또는 트러블슈팅을 설명할 수 있다.

## 9. D5 - 2026-04-30(목): main 안정화

목표:

- 발표/공유 피드백을 반영한다.
- `develop`을 제출 가능한 상태로 만든다.
- `develop -> main` merge 후보를 준비한다.

main merge 조건:

- `threads/build`에서 `make check` 성공
- test log 최신화
- 발표/제출용 문서 최신화
- debug print 없음
- 임시 TODO 정리
- 테스트 이름 기반 하드코딩 없음
- 팀원 3명이 핵심 구현 설명 가능

## 10. 이번 주 중요 제약

- 하나의 PR에 Alarm, Priority, Donation, MLFQS를 섞지 않는다.
- 실패 테스트 하나를 고치기 위해 전체 구조를 불필요하게 갈아엎지 않는다.
- interrupt context에서 sleep/block 가능한 함수를 호출하지 않는다.
- thread와 interrupt handler가 공유하는 자료구조를 수정할 때 interrupt level을 신중히 관리한다.
- ready list, sleep list, semaphore waiters, condition waiters의 invariant를 명확히 유지한다.
- priority donation에서는 base priority와 effective priority를 분리한다.
- lock release 시 해당 lock과 관련된 donation만 제거한다.
- MLFQS update timing은 timer interrupt 기준으로 맞춘다.

## 11. 바로 다음 액션

1. `rules.md`를 팀원이 함께 읽고 브랜치/commit/PR 규칙에 합의한다.
2. `feature/p1-alarm-clock` 브랜치를 만든다.
3. Driver와 Navigator / Scribe를 정한다.
4. `p1_alarm_clock_today_plan.pdf`를 보며 의사코드와 invariant를 먼저 말로 검증한다.
5. 의사코드가 합의되면 Alarm Clock 구현을 시작한다.
