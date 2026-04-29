# PintOS P1 주간 발표 계획

## 전체 발표 메시지

이번 발표는 개념 설명보다 이번 주에 팀이 구현을 진행하면서 어떤 구조를 이해했고, 어떤 트러블슈팅을 통해 구현 방향을 정리했는지를 공유하는 데 초점을 둔다.

전체 흐름은 팀 진행 방식과 진척도를 먼저 짧게 공유한 뒤, Priority Scheduling 과정에서 마주친 Sema/Condvar 구조 문제와 Priority Donation 리팩토링 및 nested donation 판단을 연결해서 설명한다.

핵심 키워드는 다음과 같다.

- PintOS의 intrusive list 구조
- `condition.waiters`와 `semaphore.waiters`의 차이
- `semaphore_elem` wrapper의 역할
- donation list의 정렬 상태를 항상 신뢰할 수 없는 이유
- `lock_acquire()`와 `donate_priority()`의 책임 분리
- chain donation 전파를 언제 멈출 수 있는지

## 6분 타임라인

| 시간 | 파트 | 발표 내용 |
| --- | --- | --- |
| 0:00-1:00 | Part 1 | 팀 진행 방식, 진척도, 팀원별 역할 |
| 1:00-3:15 | Part 2 | Sema/Condvar 트러블슈팅 |
| 3:15-5:30 | Part 3 | Priority Donation 트러블슈팅 |
| 5:30-6:00 | 회고 | 팀 회고 |

## Part 1. 팀 진행 방식과 진척도

첫 번째 파트는 별도로 준비한 내용을 사용한다.

포함할 방향은 다음 정도로만 잡는다.

- 팀이 어떤 방식으로 프로젝트를 진행했는지
- 이번 주 진척도가 어느 정도인지
- 각 팀원이 어떤 역할을 맡았는지

이 문서에서는 세부 내용을 추가하지 않는다.

## Part 2. Sema/Condvar 트러블슈팅

기준 문서:

- `pintos/docs/p1/p1_priority_scheduling/p1_priority_condvar_debug_notes.md`
- 설명 범위: 5, 6, 7, 9번

### 흐름

Priority Scheduling을 구현할 때 처음에는 ready list만 priority 순서로 관리하면 충분할 것처럼 보였다. 하지만 `priority-condvar`를 디버깅하면서 condition variable의 waiters 구조를 정확히 이해해야 했다.

핵심은 `cond_wait()`에서 condition variable이 thread를 직접 기다리게 하지 않는다는 점이다. `condition.waiters`에는 thread의 `elem`이 아니라 `semaphore_elem.elem`이 들어간다. 실제 thread는 그 `semaphore_elem` 내부의 개인 semaphore에서 block된다.

구조는 다음과 같이 정리할 수 있다.

```text
condition.waiters
  -> semaphore_elem.elem

semaphore_elem.semaphore.waiters
  -> thread.elem
```

이 구조가 필요한 이유는 PintOS의 list가 intrusive list이기 때문이다. `list_elem`은 단순한 값이 아니라 `prev`, `next` 포인터를 가진 연결 노드다. 하나의 `list_elem`을 두 리스트에 동시에 넣으면 두 번째 리스트에 삽입되는 순간 기존 연결 정보가 덮어써져서 첫 번째 리스트가 깨질 수 있다.

따라서 condition variable에서는 `thread.elem`을 condition waiters에 직접 넣을 수 없다. `thread.elem`은 개인 semaphore waiters에서 사용되어야 하고, condition waiters에는 `semaphore_elem.elem`이 들어가야 한다.

`semaphore_elem`은 condition variable에서 기다리는 "wait 호출 하나"를 표현하는 wrapper다. condition variable 자체가 thread를 직접 재우는 것이 아니라, 각 waiter마다 개인 semaphore를 만들고 그 semaphore를 통해 block/unblock을 수행한다.

이 때문에 `priority-condvar`에서는 새로운 비교 함수가 필요했다. `sema->waiters`는 `thread.elem` 리스트이므로 기존 `thread_priority_compare()`를 사용할 수 있다. 반면 `cond->waiters`는 `semaphore_elem.elem` 리스트이므로, 같은 비교 함수를 사용하면 list element를 잘못된 타입으로 해석하게 된다.

결론적으로 `cond->waiters`를 정렬하려면 `semaphore_elem`을 꺼낸 뒤, 그 안에 저장한 thread의 priority를 기준으로 비교해야 한다.

### 발표용 핵심 문장

```text
Priority Scheduling은 ready list만 정렬하면 끝나는 문제가 아니었습니다.
condition variable에서는 condition.waiters에 thread가 직접 들어가지 않고,
semaphore_elem이라는 wrapper가 들어갑니다.

PintOS list는 intrusive list라 하나의 list_elem을 두 리스트에 동시에 넣을 수 없습니다.
그래서 thread.elem은 실제로 block되는 개인 semaphore waiters에 두고,
condition.waiters에는 semaphore_elem.elem을 넣어야 했습니다.

이 구조 때문에 condvar용 priority 비교 함수도 별도로 필요했습니다.
기존 thread_priority_compare는 thread.elem 리스트를 전제로 하므로,
semaphore_elem.elem 리스트인 condition.waiters에는 그대로 사용할 수 없었습니다.
```

### 다음 파트 연결

```text
이때 배운 "하나의 list_elem은 하나의 리스트에만 들어갈 수 있다"는 규칙이
이후 Priority Donation 구현에서도 그대로 중요해졌습니다.
```

## Part 3. Priority Donation 트러블슈팅

기준 문서:

- `pintos/docs/p1/p1_donation/p1_priority_donation_debug_notes.md`
- 설명 범위: 5번
- `pintos/docs/p1/p1_donation/p1_priority_donation_implementation_log.md`
- 설명 범위: 리팩토링 내용 중 책임 분리, Chain 전파 중단 조건

### donations 리스트의 front를 항상 믿으면 안 되는 이유

처음에는 `donations` 리스트를 삽입 시점에 priority 순으로 넣으면 이후에는 front만 보면 가장 높은 donor를 알 수 있다고 생각할 수 있다. 하지만 nested donation이 들어가면 이 가정이 깨진다.

예를 들어 donor A가 holder H의 donations 리스트에 들어갈 때는 priority가 32였더라도, 이후 A가 다른 높은 priority thread에게 nested donation을 받아 effective priority가 45로 올라갈 수 있다. 이 경우 H의 donations 리스트 안에서 A의 위치는 삽입 당시 기준으로는 맞지만, 현재 priority 기준으로는 더 이상 맞지 않을 수 있다.

따라서 `refresh_priority()`에서 donations 리스트의 front를 보기 전에 현재 priority 기준으로 다시 정렬한다.

```c
list_sort(&t->donations, donor_priority_compare, NULL);
```

중요한 점은 "삽입 당시 정렬 상태가 현재도 유효하다"는 가정을 하지 않는 것이다.

### `lock_acquire()`와 `donate_priority()` 책임 분리

초기 구현에서는 `lock_acquire()` 안에서 donation을 걸고, holder chain을 따라 올라가며 nested donation을 전파하는 로직까지 직접 처리했다. 이 방식도 테스트를 통과할 수는 있지만, `lock_acquire()`가 lock 획득 절차와 donation 전파 정책을 동시에 책임지게 되어 함수의 역할이 흐려졌다.

리팩토링 후 책임은 다음과 같이 나뉜다.

`lock_acquire()`의 책임:

- 현재 thread의 `wait_lock` 설정
- lock holder 확인
- donation이 필요하면 `donate_priority()` 호출
- `sema_down()`으로 실제 lock 대기
- lock 획득 후 `wait_lock` 해제와 holder 갱신

`donate_priority()`의 책임:

- receiver의 donations 리스트에 donor 등록
- receiver priority refresh
- receiver가 다른 lock을 기다리면 그 holder에게 priority 전파
- 더 이상 priority가 올라가지 않으면 chain 전파 중단

이렇게 나누면 `lock_acquire()`는 lock을 얻는 흐름에 집중하고, donation 정책은 `donate_priority()` 안에서 따로 확인할 수 있다. 이후 nested donation 로직을 조정해야 할 때도 `lock_acquire()`를 크게 건드리지 않고 helper 내부에서 판단할 수 있다.

### Chain 전파 중단 조건

리팩토링 후 `donate_priority()`의 chain 전파에서는 다음 조건을 사용한다.

```c
if (donor->priority > receiver->priority) {
	receiver->priority = donor->priority;
}
else {
	break;
}
```

이 조건의 의미는 현재 donor priority로 receiver의 effective priority를 더 올릴 수 있을 때만 전파를 계속한다는 것이다. receiver가 이미 donor보다 높거나 같은 effective priority를 가지고 있다면, 그 위쪽 holder에게도 이번 donor 때문에 새로 더 높은 priority를 전달할 필요가 없다.

다만 이 논리는 priority를 올리는 donation 전파 경로에만 적용된다. lock release 후 donation이 사라져 priority가 내려가야 하는 경우에는 `remove_donation()`과 `refresh_priority()`가 별도로 처리한다. chain 전파의 `break`는 새 donation으로 올릴 값이 있는지에 대한 최적화이지, donation 제거 로직을 대체하지 않는다.

### 발표용 핵심 문장

```text
Donation에서는 donations 리스트를 priority 순으로 넣어두면 front만 보면 된다고 생각했지만,
nested donation 때문에 donor의 effective priority가 삽입 이후에도 바뀔 수 있었습니다.
그래서 refresh_priority()에서 donations 리스트를 다시 정렬한 뒤 가장 높은 donor를 확인했습니다.

또 lock_acquire() 안에 donation chain 전파까지 모두 넣으면 함수 책임이 너무 커졌습니다.
그래서 lock_acquire()는 lock 획득 흐름만 맡기고,
donation 관계 등록과 nested chain 전파는 donate_priority()로 분리했습니다.

chain 전파는 donor priority가 receiver priority보다 높을 때만 계속합니다.
이미 receiver가 더 높거나 같다면 이번 donor로 더 올릴 값이 없기 때문에 전파를 멈춥니다.
다만 priority가 내려가는 상황은 별도 문제라 remove_donation()과 refresh_priority()에서 처리했습니다.
```

## 마지막 30초 회고

이번 주에 가장 크게 배운 점은 테스트를 통과시키는 코드보다, 어떤 리스트에 어떤 `elem`이 들어가고 언제 제거되는지를 먼저 설명할 수 있어야 한다는 점이다.

Scheduler, semaphore, condition variable, donation이 모두 같은 list invariant 위에 연결되어 있었기 때문에, 기능별로 따로 보기보다 상태 전이를 함께 봐야 디버깅이 가능했다.

다음 구현부터는 코드 작성 전에 자료구조의 소유권, list element의 용도, 상태 복구 조건을 먼저 맞추고 들어가는 방식으로 진행한다.

## 기준 문서와 발표 기준 브랜치

발표 기준 문서:

- `docs` 브랜치의 `pintos/docs/p1/p1_priority_scheduling/p1_priority_condvar_debug_notes.md`
- `docs` 브랜치의 `pintos/docs/p1/p1_donation/p1_priority_donation_debug_notes.md`
- `docs` 브랜치의 `pintos/docs/p1/p1_donation/p1_priority_donation_implementation_log.md`

구현 기준:

- Priority Scheduling / Priority Donation 구현은 `develop` 브랜치 기준으로 설명한다.
- 첫 번째 파트의 팀 진행 방식과 진척도 세부 내용은 별도 자료를 사용한다.
