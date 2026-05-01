# PintOS Project 1 - Priority Donation 구현 기록

## 1. 맥락

이번 구현은 `feature/p1-priority-scheduling` 브랜치의 priority scheduling 구현 위에서 진행했다. 이전 단계에서 `ready_list`, semaphore waiters, condition variable waiters는 이미 priority 기준으로 정렬되도록 바뀌어 있었다. 따라서 donation 구현의 핵심은 새로운 scheduler를 만드는 것이 아니라, 기존 scheduler가 참고하는 `thread->priority` 값을 lock 대기 관계에 맞게 임시로 올리고 되돌리는 것이었다.

Priority Donation이 필요한 문제는 priority inversion이다. 낮은 priority thread가 lock을 가지고 있고, 높은 priority thread가 그 lock을 기다리면 높은 priority thread가 block된다. 이때 lock holder가 낮은 priority 그대로 남아 있으면 중간 priority thread들에게 CPU를 빼앗겨 lock release가 늦어질 수 있다. 이를 막기 위해 lock을 기다리는 높은 priority thread가 lock holder에게 자신의 priority를 임시로 빌려준다.

이번 구현에서는 `effective_priority`라는 별도 필드를 추가하지 않고, 기존 `priority` 필드를 effective priority로 사용했다. 대신 `base_priority`를 새로 추가해 thread의 원래 priority를 보관했다. 이 결정은 기존 priority scheduling 코드가 이미 `priority`를 기준으로 동작하고 있었기 때문에, 변경 범위를 줄이면서 donation을 얹기 위한 선택이다.

구현의 1차 목표는 `priority-donate-one`, `priority-donate-multiple`, `priority-donate-multiple2`, `priority-donate-lower`를 안정적으로 통과시키는 것이었다. 이후 `priority-donate-nest`, `priority-donate-chain`, `priority-donate-sema`까지 확인하면서 nested donation과 semaphore waiters 재정렬 문제를 함께 다뤘다.

관련 커밋은 다음 흐름으로 정리할 수 있다.

```text
544469d feat(donation): Implemented priority donation
71bd674 refactor(donation): donate_priority()와 lock_acquire() 책임 분리
```

`544469d`에서는 donation 필드와 helper, `lock_acquire()`, `lock_release()`, `thread_set_priority()`의 기본 동작을 구현했다. `71bd674`에서는 `lock_acquire()`에 있던 donation chain 전파 책임을 `donate_priority()`로 옮겨 함수 책임을 더 명확하게 나눴다.

## 2. 코드 수정한 부분

### `pintos/include/threads/thread.h`

Donation을 추적하기 위해 `struct thread`에 priority 관련 상태와 donation list 관련 필드를 추가했다.

```c
int priority;                       /* donation이 반영된 유효 우선순위. */
int base_priority;                  /* donation을 제외한 원래 priority. */

struct list_elem donation_elem;     /* 다른 thread의 donations 리스트에 연결될 때 쓰는 elem. */
struct list donations;              /* 이 thread에게 priority를 donation한 donor 목록. */
struct lock *wait_lock;             /* 현재 기다리는 lock. 기다리는 lock이 없으면 NULL. */
```

`priority`는 scheduler가 실제로 참고하는 effective priority다. `base_priority`는 사용자가 `thread_set_priority()`로 직접 바꾸는 원래 priority다. donation을 받고 있지 않으면 두 값은 같고, donation을 받고 있으면 `priority`가 `base_priority`보다 높을 수 있다.

`donations`는 현재 thread에게 priority를 donation한 donor thread들의 리스트다. `donation_elem`은 이 thread가 다른 thread의 `donations` 리스트에 들어갈 때 사용하는 별도 list element다. 기존 `thread.elem`은 ready list와 semaphore waiters에서 이미 사용되므로 donation list에 재사용하지 않는다.

`wait_lock`은 현재 thread가 어떤 lock을 기다리는지 기록한다. lock release 때는 donor의 `wait_lock`이 방금 release하는 lock과 같은지 확인해, 해당 lock 때문에 생긴 donation만 제거한다.

### `pintos/threads/thread.c`

`init_thread()`에서는 donation 관련 필드를 초기화했다.

```c
t->priority = priority;
t->base_priority = priority;
t->wait_lock = NULL;
list_init(&t->donations);
```

새 thread는 donation을 받은 적이 없으므로 시작 시점의 effective priority와 base priority가 같다. `donations`는 비어 있어야 하고, 아직 기다리는 lock이 없으므로 `wait_lock`은 `NULL`이다.

`donor_priority_compare()`는 `donations` 리스트를 donor thread의 effective priority 내림차순으로 정렬하기 위한 비교 함수다.

```c
bool donor_priority_compare(const struct list_elem *a,
                            const struct list_elem *b,
                            void *aux UNUSED)
{
	const struct thread *da = list_entry(a, struct thread, donation_elem);
	const struct thread *db = list_entry(b, struct thread, donation_elem);

	return da->priority > db->priority;
}
```

`donations` 리스트에는 `thread.elem`이 아니라 `thread.donation_elem`이 들어간다. 따라서 기존 `thread_priority_compare()`를 그대로 쓰면 안 되고, `list_entry()`도 `donation_elem` 기준으로 복원해야 한다.

`refresh_priority()`는 thread의 effective priority를 다시 계산한다.

```c
void refresh_priority(struct thread *t)
{
	t->priority = t->base_priority;

	if (!list_empty(&t->donations))
	{
		list_sort(&t->donations, donor_priority_compare, NULL);
		struct thread *donor = list_entry(list_front(&t->donations),
				struct thread, donation_elem);
		if (donor->priority > t->priority)
		{
			t->priority = donor->priority;
		}
	}
}
```

계산은 항상 `base_priority`에서 시작한다. 그 뒤 남아 있는 donation 중 가장 높은 donor priority를 반영한다. nested donation 과정에서 donor의 effective priority가 삽입 이후 바뀔 수 있으므로, front를 보기 전에 `donations` 리스트를 다시 정렬한다.

`donate_priority()`는 donor가 receiver에게 priority를 donation하고, 필요한 경우 holder chain 위쪽으로 priority를 전파한다.

```c
void donate_priority (struct thread *donor, struct thread *receiver) {
	if (donor == NULL || receiver == NULL) {
		return;
	}

	list_insert_ordered(&receiver->donations, &donor->donation_elem,
			donor_priority_compare, NULL);
	refresh_priority(receiver);

	while (receiver->wait_lock != NULL && receiver->wait_lock->holder != NULL) {
		receiver = receiver->wait_lock->holder;

		if (donor->priority > receiver->priority) {
			receiver->priority = donor->priority;
		}
		else {
			break;
		}
	}
}
```

직접 donation 관계는 receiver의 `donations` 리스트에 기록한다. 이 기록은 나중에 receiver가 lock을 release할 때 어떤 donation을 제거할지 판단하는 근거가 된다. receiver가 다시 다른 lock을 기다리고 있다면, 그 lock의 holder에게도 priority를 전파해 nested donation을 처리한다.

위쪽 holder들에는 같은 `donation_elem`을 다시 넣지 않는다. 하나의 `list_elem`은 동시에 여러 리스트에 들어갈 수 없기 때문이다. 대신 위쪽 holder의 effective priority 값만 끌어올리는 방식으로 chain 전파를 처리한다.

`remove_donation()`은 현재 thread가 lock을 release할 때, 해당 lock 때문에 받은 donation만 제거한다.

```c
void remove_donation (struct lock *lock) {
	struct thread *curr = thread_current();
	struct list_elem *e = list_begin (&curr->donations);

	while (e != list_end (&curr->donations)) {
		struct thread *donor = list_entry(e, struct thread, donation_elem);
		struct list_elem *next = list_next(e);

		if (donor->wait_lock == lock) {
			list_remove(e);
		}
		e = next;
	}
}
```

핵심은 모든 donation을 지우지 않고, `donor->wait_lock == lock`인 항목만 제거하는 것이다. 현재 thread가 lock A와 lock B를 둘 다 들고 있을 수 있으므로, lock A release 때 lock B 때문에 들어온 donation까지 제거하면 안 된다.

`thread_set_priority()`는 effective priority를 직접 덮어쓰지 않고 base priority를 갱신한다.

```c
void thread_set_priority(int new_priority)
{
	struct thread *curr = thread_current();

	curr->base_priority = new_priority;
	refresh_priority(curr);

	if (!list_empty(&ready_list))
	{
		struct thread *front = list_entry(list_front(&ready_list),
				struct thread, elem);

		if (front->priority > curr->priority)
		{
			thread_yield();
		}
	}
}
```

현재 thread가 donation을 받고 있다면 `base_priority`를 낮춰도 effective priority는 donation 값으로 유지되어야 한다. 그래서 base를 먼저 바꾼 뒤 `refresh_priority()`로 현재 donation 상태를 다시 반영한다. 그 결과 ready 상태의 다른 thread가 더 높은 priority를 가지면 CPU를 양보한다.

### `pintos/threads/synch.c`

`lock_acquire()`에서는 현재 thread가 어떤 lock을 기다리는지 기록하고, lock holder가 있으면 donation을 시작한다.

```c
void lock_acquire(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));

	struct thread *curr = thread_current();
	struct thread *holder = lock->holder;

	curr->wait_lock = lock;

	if (holder != NULL) {
		if (holder->priority < curr->priority) {
			donate_priority (curr, holder);
		}
	}

	sema_down(&lock->semaphore);
	curr->wait_lock = NULL;
	lock->holder = curr;
}
```

Donation은 `sema_down()`으로 block될 수 있는 구간에 들어가기 전에 걸어야 한다. 이 시점에 현재 thread가 기다리는 lock과 그 holder가 명확하기 때문이다. lock 획득에 성공하면 더 이상 기다리는 lock이 없으므로 `wait_lock`을 `NULL`로 되돌리고, lock의 holder를 현재 thread로 설정한다.

`lock_release()`에서는 해당 lock과 관련된 donation만 제거한 뒤 priority를 다시 계산한다.

```c
void lock_release(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));

	remove_donation (lock);
	refresh_priority (thread_current ());

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}
```

`remove_donation()`과 `refresh_priority()`를 `sema_up()`보다 먼저 호출한다. lock을 놓는 thread의 priority를 먼저 정상화한 뒤, semaphore waiters 중 하나를 깨운다. `sema_up()`은 waiters를 최신 priority 기준으로 다시 정렬하고, 필요하면 더 높은 priority thread에게 CPU를 양보한다.

## 3. 리팩토링 내용

초기 구현에서는 `lock_acquire()` 안에서 donation을 걸고, holder chain을 따라 올라가며 nested donation을 전파하는 로직까지 직접 처리했다. 이 방식도 테스트를 통과할 수는 있지만, `lock_acquire()`가 lock 획득 절차와 donation 전파 세부사항을 동시에 책임지게 되어 함수의 역할이 흐려졌다.

리팩토링 후에는 `lock_acquire()`가 다음 책임만 가진다.

```text
현재 thread의 wait_lock 설정
lock holder 확인
donation이 필요하면 donate_priority() 호출
sema_down()으로 실제 lock 대기
lock 획득 후 wait_lock 해제와 holder 갱신
```

반대로 `donate_priority()`는 donation 관계 기록과 nested donation chain 전파를 담당한다.

```text
receiver의 donations 리스트에 donor 등록
receiver priority refresh
receiver가 다른 lock을 기다리면 그 holder에게 priority 전파
더 이상 priority가 올라가지 않으면 chain 전파 중단
```

이렇게 나누면 `lock_acquire()`는 "lock을 얻는 함수"로 읽히고, donation 정책은 `donate_priority()` 안에서 따로 확인할 수 있다. 이후 nested donation 로직을 조정해야 할 때도 `lock_acquire()`를 크게 건드리지 않고 helper 내부에서 판단할 수 있다.

리팩토링 중 주의한 지점은 `donation_elem`의 소유 관계다. 직접 기다리는 lock holder의 `donations` 리스트에는 donor를 넣지만, chain 위쪽 holder들의 `donations` 리스트에는 같은 donor를 다시 넣지 않는다. 같은 `donation_elem`을 여러 list에 넣으면 intrusive list의 `prev`, `next` 포인터가 덮어써져 리스트가 깨지기 때문이다.

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

이 `break`는 "현재 donor priority로는 이 receiver의 effective priority를 더 올릴 수 없다"는 뜻이다. receiver가 이미 donor보다 높거나 같은 effective priority를 가지고 있다면, 그 위쪽 holder에게도 이번 donor 때문에 새로 더 높은 priority를 전달할 필요가 없다.

즉 이 조건은 priority를 올리는 donation 전파 경로에서는 합리적이다. 불필요하게 chain 끝까지 올라가지 않아도 되므로, chain propagation을 `donate_priority()` 안으로 옮긴 뒤에도 동작 의도가 비교적 분명하다.

다만 이 논리는 priority를 낮추는 상황에는 적용되지 않는다. lock release 후 donation이 사라져 priority가 내려가야 하는 경우에는 `remove_donation()`과 `refresh_priority()`가 별도로 처리해야 한다. chain 전파의 `break`는 "새 donation으로 올릴 값이 있는가"에 대한 최적화이지, donation 제거 로직을 대체하지 않는다.

### `lock_acquire()`의 ASSERT 위치

리팩토링 과정에서 `lock_acquire()`는 lock 내부 필드를 읽기 전에 먼저 전제조건을 확인해야 한다는 점도 정리했다. 다음 순서는 안전하지 않다.

```c
struct thread *holder = lock->holder;

ASSERT(lock != NULL);
```

`ASSERT(lock != NULL)`이 있어도 이미 그 전에 `lock->holder`로 lock을 역참조했기 때문에, lock이 NULL이면 ASSERT에 도달하기 전에 잘못된 메모리 접근이 발생한다.

따라서 `lock_acquire()`에서는 먼저 ASSERT로 전제조건을 확인하고, 그 다음에 lock 내부 필드를 읽는 순서를 유지한다.

```c
ASSERT(lock != NULL);
ASSERT(!intr_context());
ASSERT(!lock_held_by_current_thread(lock));

struct thread *curr = thread_current();
struct thread *holder = lock->holder;
```

PintOS 테스트가 `lock_acquire(NULL)`을 직접 호출하지 않더라도, 커널 코드에서는 이런 순서가 중요하다. ASSERT는 문서화된 전제조건이면서 디버깅 장치이므로, 전제조건을 확인하기 전에 그 포인터를 사용하면 의미가 약해진다.

## 4. 유지해야 하는 invariant

`thread->priority`는 donation이 반영된 effective priority다. scheduler, ready list 비교, semaphore waiters 비교, `thread_get_priority()`는 이 값을 기준으로 동작한다.

`thread->base_priority`는 donation을 제외한 원래 priority다. `thread_set_priority()`는 이 값을 바꾸는 함수로 해석한다.

`thread->donations`에는 현재 thread에게 priority를 빌려준 donor thread들이 들어간다. 이 리스트에 들어가는 list element는 반드시 `donation_elem`이어야 한다.

하나의 `list_elem`은 동시에 두 리스트에 들어갈 수 없다. lock을 기다리는 donor thread는 semaphore waiters에 `thread.elem`으로 들어가면서 holder의 donations 리스트에 `donation_elem`으로도 들어가야 하므로, 두 elem은 분리되어야 한다.

`thread->wait_lock`은 donation의 원인을 나타낸다. lock release 시에는 `donor->wait_lock == lock`인 donation만 제거해야 한다.

`refresh_priority()`는 donations 리스트의 기존 순서를 맹신하면 안 된다. donor의 priority는 nested donation으로 삽입 이후에도 바뀔 수 있으므로, 가장 높은 donor를 고르기 전에 정렬하거나 전체 순회로 최댓값을 찾아야 한다.

MLFQS에서는 priority donation을 적용하지 않는다. Project 1의 donation 구현은 기본 priority scheduler 기준으로만 유지한다.

## 5. 테스트 확인 범위

이번 구현에서 우선적으로 확인한 donation 테스트는 다음과 같다.

```text
priority-donate-one
priority-donate-multiple
priority-donate-multiple2
priority-donate-lower
priority-donate-nest
priority-donate-chain
priority-donate-sema
```

각 테스트의 핵심 확인 내용은 다음과 같다.

- `priority-donate-one`: 높은 priority thread 하나가 lock holder에게 donation하는지 확인한다.
- `priority-donate-multiple`: 여러 lock에서 들어온 donation 중 가장 높은 priority가 유지되는지 확인한다.
- `priority-donate-multiple2`: 같은 lock 또는 여러 donor가 얽힌 상황에서 donation 제거가 정확한지 확인한다.
- `priority-donate-lower`: donation 중 `thread_set_priority()`로 base priority를 낮춰도 effective priority가 유지되는지 확인한다.
- `priority-donate-nest`: lock holder가 다시 다른 lock을 기다릴 때 donation이 한 단계 이상 전파되는지 확인한다.
- `priority-donate-chain`: 여러 단계의 nested donation chain에서 priority가 위쪽 holder까지 전달되는지 확인한다.
- `priority-donate-sema`: semaphore waiters 선택이 최신 effective priority를 기준으로 이루어지는지 확인한다.

권장 테스트 명령은 다음과 같다.

```bash
cd pintos/threads
make clean
make
cd build
make tests/threads/priority-donate-one.result
make tests/threads/priority-donate-multiple.result
make tests/threads/priority-donate-multiple2.result
make tests/threads/priority-donate-lower.result
make tests/threads/priority-donate-nest.result
make tests/threads/priority-donate-chain.result
make tests/threads/priority-donate-sema.result
```
