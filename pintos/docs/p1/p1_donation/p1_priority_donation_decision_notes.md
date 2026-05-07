# PintOS Project 1 - Priority Donation 결정 사항 정리

## 1. 현재 결정 요약

Priority Donation은 `feature/p1-priority-scheduling` 브랜치의 최신 priority scheduling 구현 위에서 진행한다. 현재 코드에서는 `thread_priority_compare()`가 `thread->priority`를 기준으로 ready list와 semaphore waiters를 비교하고 있으므로, 이 흐름을 최대한 유지한다.

이번 base 구현에서는 `effective_priority`라는 새 필드를 추가하지 않는다. 대신 기존 `priority` 필드를 effective priority로 계속 사용하고, 새 필드 `base_priority`만 `struct thread`에 추가한다.

```text
priority      = 현재 scheduler가 실제로 사용하는 effective priority
base_priority = thread_set_priority()로 직접 설정되는 원래 priority
```

이 결정은 기존 priority scheduling 코드의 수정 범위를 줄이기 위한 것이다. 이미 ready list, semaphore waiters, preemption 판단, `thread_get_priority()`가 `priority`를 기준으로 동작하고 있으므로, `priority`의 의미를 effective priority로 유지하면 donation 구현을 비교적 작게 얹을 수 있다.

## 2. `struct thread`에 추가할 필드

Donation을 정확히 관리하려면 스레드가 자신의 원래 priority와 donation 관계를 기억해야 한다. 특히 lock release 시점에는 "이 donation이 지금 release하는 lock 때문에 들어온 것인가?"를 판단해야 하므로, 단순히 priority 값만 저장하는 방식은 부족하다.

추가할 필드는 다음 방향으로 정한다.

```c
struct thread
{
  int priority;                   /* 현재 effective priority */
  int base_priority;              /* 원래 priority */

  struct list donations;          /* 나에게 priority를 donation한 thread들의 리스트 */
  struct list_elem donation_elem; /* 다른 thread의 donations 리스트에 들어갈 때 쓰는 elem */

  struct lock *wait_lock;         /* 현재 기다리는 lock, 없으면 NULL */
};
```

`priority`는 기존 필드이며 새로 추가하지 않는다. 다만 donation 구현 이후부터는 이 필드를 "현재 effective priority"로 해석한다. 새로 추가하는 priority 값 필드는 `base_priority` 하나다.

`donations`는 현재 thread에게 priority를 빌려준 donor thread들의 목록이다. `donation_elem`은 이 thread가 다른 thread의 `donations` 리스트에 들어갈 때 사용하는 list element다. `wait_lock`은 이 thread가 현재 어떤 lock 때문에 block되려 하는지 기록한다.

기존 `struct thread`의 `elem`을 donation list에 재사용하지 않는다. `elem`은 이미 ready list, semaphore waiters, sleep list 같은 실행 상태 관련 리스트에 들어갈 수 있는 멤버다. 특히 lock을 기다리는 donor thread는 lock 내부 semaphore의 waiters에 `elem`으로 들어가면서, 동시에 lock holder의 `donations` 리스트에도 등록되어야 한다. 하나의 `list_elem`은 동시에 두 리스트에 들어갈 수 없으므로, donation 관계를 기록하려면 별도의 `donation_elem`이 필요하다.

## 3. `init_thread()` 초기화 정책

새 스레드가 만들어질 때 donation 관련 값은 항상 깨끗한 초기 상태여야 한다. 초기에는 donation을 받은 적이 없으므로 effective priority와 base priority가 동일하다.

`init_thread()`에서는 다음 상태를 만든다.

```text
t->priority      = priority
t->base_priority = priority
t->wait_lock     = NULL
list_init(&t->donations)
```

이 초기화가 빠지면 이후 lock release나 priority refresh 과정에서 초기화되지 않은 리스트를 순회하거나, 의미 없는 lock 포인터를 기준으로 donation 제거를 시도할 수 있다. 따라서 donation 관련 필드는 thread 생성 시점에 반드시 초기화한다.

## 4. `thread_get_priority()` 의미

`thread_get_priority()`는 현재 thread의 effective priority를 반환해야 한다. 이번 설계에서는 기존 `priority` 필드가 effective priority이므로 함수의 반환식 자체는 크게 바뀌지 않는다.

```c
int
thread_get_priority (void)
{
  return thread_current ()->priority;
}
```

중요한 점은 이 함수가 `base_priority`를 반환하면 안 된다는 것이다. donation을 받은 상태에서는 현재 스케줄링에서 적용되는 값이 `base_priority`보다 높을 수 있고, 테스트는 이 effective priority를 기준으로 현재 priority를 확인한다.

## 5. `thread_set_priority()` 정책

`thread_set_priority()`는 effective priority를 직접 덮어쓰는 함수가 아니라, 현재 thread의 `base_priority`를 바꾸는 함수로 해석한다.

예를 들어 main thread의 원래 priority가 31이고, priority 41 thread가 main이 가진 lock을 기다리고 있다고 하자. 이때 main이 `thread_set_priority(21)`을 호출하더라도, main이 lock을 들고 있는 동안 effective priority는 41로 유지되어야 한다.

```text
base priority: 31 -> 21
donation: 41 유지
effective priority: 41 유지
```

따라서 `thread_set_priority()`는 먼저 `base_priority`를 갱신하고, 이후 현재 남아 있는 donation을 기준으로 `priority`를 다시 계산해야 한다. donation이 남아 있으면 `priority`는 donation 중 최댓값을 반영하고, donation이 없다면 `base_priority`로 돌아간다.

## 6. Donation helper 결정

base 구현에서는 donation 관리를 위해 helper를 세 종류로 나눈다. 구현 중 비효율적이거나 함수 경계가 어색한 부분은 조정할 수 있지만, 첫 구현 계획은 아래 책임 분리를 기준으로 한다.

첫 번째 helper는 donation을 추가하고 전파하는 역할이다.

```text
"나 때문에 기다리는 thread가 생겼으니 holder의 priority를 올려야 한다."
```

이 helper는 `lock_acquire()`에서 lock holder가 존재할 때 사용한다. 현재 thread의 `wait_lock`을 설정한 뒤, holder의 `donations` 목록에 현재 thread를 donor로 연결하고, holder의 effective priority를 올린다. nested donation을 위해 holder가 다시 다른 lock을 기다리고 있으면 제한 깊이 안에서 donation을 이어 전달한다.

두 번째 helper는 특정 lock과 관련된 donation만 제거하는 역할이다.

```text
"이 lock은 release했으니, 이 lock 때문에 받은 donation은 제거한다."
```

이 helper는 `lock_release()`에서 사용한다. 현재 thread의 `donations` 목록을 순회하면서 donor의 `wait_lock`이 방금 release하는 lock인 항목만 제거한다. 다른 lock 때문에 들어온 donation은 유지해야 한다.

세 번째 helper는 effective priority를 다시 계산하는 역할이다.

```text
"base priority와 남은 donation 중 가장 큰 값을 다시 계산한다."
```

이 helper는 `lock_release()`와 `thread_set_priority()`에서 사용한다. 기본값은 `base_priority`이고, 남아 있는 donor thread들의 `priority` 중 더 큰 값이 있으면 그것을 현재 thread의 `priority`로 반영한다.

## 7. `lock_acquire()`에서 donation을 거는 시점

Donation은 높은 priority thread가 lock을 얻지 못하고 block되기 직전에 걸어야 한다. 이 시점에 "현재 thread가 어떤 lock을 기다리는지"와 "그 lock의 holder가 누구인지"가 명확해진다.

계획은 다음 순서다.

```text
lock holder가 존재한다.
현재 thread의 wait_lock을 lock으로 설정한다.
현재 thread가 holder에게 donation한다.
sema_down(&lock->semaphore)로 block될 수 있는 구간에 들어간다.
lock 획득에 성공하면 wait_lock을 NULL로 되돌린다.
lock->holder를 현재 thread로 설정한다.
```

`lock_acquire()`는 interrupt handler에서 호출할 수 없고 sleep 가능한 함수다. 따라서 donation 설정은 `sema_down()`으로 실제 block되기 전, lock holder가 아직 존재하는 시점에 수행해야 한다.

## 8. `lock_release()`에서 donation을 제거하는 시점

Lock을 release하면 그 lock 때문에 받은 donation은 더 이상 유지하면 안 된다. 다만 현재 thread가 여러 lock을 들고 있을 수 있으므로 모든 donation을 제거하면 안 되고, release하는 lock과 연결된 donation만 제거해야 한다.

계획은 다음 순서다.

```text
현재 thread가 lock holder인지 확인한다.
현재 thread의 donations에서 release하는 lock 관련 donation만 제거한다.
남은 donation과 base_priority를 기준으로 priority를 refresh한다.
lock->holder를 NULL로 설정한다.
sema_up(&lock->semaphore)를 호출한다.
```

핵심 invariant는 `donor->wait_lock == lock`인 donation만 제거한다는 점이다. 이 규칙을 지키면 lock A 때문에 받은 donation은 lock A release 때 사라지고, lock B 때문에 받은 donation은 lock B를 release할 때까지 유지된다.

## 9. `sema_up()`에서 waiters를 최신 effective 기준으로 선택

현재 priority scheduling 구현은 `sema_down()`에서 waiters를 priority 기준으로 정렬 삽입한다. 하지만 donation이 들어오면 waiters 리스트에 들어간 뒤에도 thread의 effective priority가 바뀔 수 있다.

따라서 `sema_up()`에서는 깨우기 직전에 waiters를 최신 `priority` 기준으로 다시 정렬하거나, 최신 `priority`가 가장 높은 thread를 선택해야 한다.

```text
waiters에 들어간 시점의 priority만 믿지 않는다.
깨우는 시점의 effective priority를 기준으로 선택한다.
```

이번 base 계획에서는 구현 안정성을 위해 `sema_up()` 직전에 waiters를 재정렬하는 방향을 우선 고려한다. 테스트 규모에서는 O(n log n) 정렬 비용보다 correctness가 더 중요하다.

## 10. Base 구현에서 우선 목표로 삼을 테스트

첫 donation 구현은 한 번에 모든 복잡한 케이스를 완벽히 일반화하기보다, base donation 테스트를 통과시키는 것을 우선 목표로 한다.

우선순위는 다음 순서로 둔다.

```text
priority-donate-one
priority-donate-multiple
priority-donate-multiple2
priority-donate-lower
priority-donate-nest
priority-donate-chain
priority-donate-sema
```

`priority-donate-one`, `priority-donate-multiple`, `priority-donate-lower`는 base/effective priority 분리와 lock release 시 donation 제거가 제대로 되었는지 확인하는 핵심 테스트다. `priority-donate-nest`, `priority-donate-chain`은 nested donation 전파가 필요한 테스트이므로 helper 구조가 안정된 뒤 확인한다. `priority-donate-sema`는 semaphore waiters 선택 시 최신 effective priority 반영이 필요한 회귀 테스트로 본다.

## 11. 구현 중 조정 가능한 부분

이 문서는 구현 전 기준 계획이다. 실제 구현 중 다음 부분은 코드 복잡도와 테스트 결과에 따라 조정할 수 있다.

```text
donation helper 함수의 개수와 이름
donation list 정렬 유지 여부
refresh 시 매번 donations 전체 순회 여부
nested donation 전파 깊이 제한 구현 방식
sema_up()에서 재정렬할지, max 원소를 찾아 제거할지
```

다만 아래 결정은 유지한다.

```text
기존 priority 필드는 effective priority로 사용한다.
base_priority 필드를 새로 추가한다.
lock release 시 해당 lock 관련 donation만 제거한다.
MLFQS에서는 priority donation을 적용하지 않는다.
```
