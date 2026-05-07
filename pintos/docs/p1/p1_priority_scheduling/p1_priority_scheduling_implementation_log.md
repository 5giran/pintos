# PintOS Project 1 - Priority Scheduling 구현 기록

## 1. 맥락

이번 구현에서는 기존 `ready_list`의 FIFO 기반 스케줄링 흐름을 priority 기반 스케줄링으로 변경했다. 기존 코드는 실행 가능한 thread를 `ready_list` 뒤에 단순 삽입하고, 다음에 실행할 thread를 리스트 앞에서 꺼내는 구조였다. 이 방식은 thread의 priority 값을 스케줄링 판단에 사용하지 않기 때문에, 높은 priority thread가 준비 상태가 되어도 낮은 priority thread 뒤에서 기다릴 수 있었다.

이번 변경에서는 `ready_list`를 항상 priority 내림차순으로 유지하도록 만들었다. 높은 priority thread가 리스트 앞쪽에 위치하므로, 기존의 `next_thread_to_run()`이 `list_pop_front()`로 다음 thread를 선택하는 구조를 유지하면서도 가장 높은 priority thread를 먼저 실행할 수 있다.

또한 새 thread가 생성되거나 현재 thread의 priority가 낮아졌을 때, ready 상태의 더 높은 priority thread에게 CPU를 즉시 양보하도록 처리했다. 이를 통해 단순히 다음 timer tick이나 다음 yield 시점까지 기다리는 것이 아니라, priority scheduling에서 기대하는 즉시 선점 동작을 구현했다.

이번 Priority Scheduling 구현은 Alarm Clock 구현이 끝난 `a89adb2 fix(timer): removed while loop in timer_sleep()` 커밋 위에서 시작되었다. priority scheduling의 기본 구현은 `7462084 feat(priority): change FIFO to Priority scheduling`에서 진행했고, 이후 코드 리뷰를 거쳐 `6c03228 fix(priority): 주석 한글로 변경, thread_set_priority 중복 코드 제거`에서 한글 주석 정리와 `thread_set_priority()` 중복 호출 제거를 반영했다.

해당 변경사항은 `7b5500f Merge pull request #40 from 5giran/feature/p1-priority-scheduling`에서 `develop` 브랜치에 merge되었고, 현재 feature 브랜치에도 `develop`의 최신 변경사항을 반영했다. 실제 priority scheduling 관련 코드 변경은 `pintos/threads/thread.c`에 집중되어 있다.

## 2. 코드 수정한 부분

### `pintos/threads/thread.c`

`ready_list`에 들어가는 thread를 priority 기준으로 정렬하기 위해 비교 함수 `thread_priority_compare()`를 추가했다.

```c
static bool
thread_priority_compare (const struct list_elem *a,
		const struct list_elem *b,
		void *aux UNUSED) {
	const struct thread *ta = list_entry (a, struct thread, elem);
	const struct thread *tb = list_entry (b, struct thread, elem);

	return ta->priority > tb->priority;
}
```

이 함수는 두 `struct list_elem`을 `struct thread`로 복원한 뒤, 더 높은 priority를 가진 thread가 리스트 앞쪽에 오도록 비교한다. priority가 같은 경우에는 `false`를 반환하므로, `list_insert_ordered()`가 기존 같은 priority thread들의 상대 순서를 유지한다. 이 덕분에 같은 priority끼리는 FIFO 순서를 보장할 수 있다.

`thread_unblock()`에서는 기존의 단순 뒤 삽입을 priority 정렬 삽입으로 변경했다.

```c
list_insert_ordered (&ready_list, &t->elem, thread_priority_compare, NULL);
```

기존 구현은 unblock된 thread를 항상 `ready_list` 뒤에 추가했다. 변경 후에는 unblock되는 순간부터 `ready_list`가 priority 내림차순을 유지한다. 따라서 semaphore, timer, thread 생성 등 어떤 경로에서 thread가 ready 상태로 돌아오더라도 scheduler는 가장 높은 priority thread를 먼저 선택할 수 있다.

`thread_yield()`에서도 현재 thread를 다시 ready 상태로 넣을 때 priority 정렬 삽입을 사용하도록 변경했다.

```c
if (curr != idle_thread) {
	list_insert_ordered (&ready_list, &curr->elem, thread_priority_compare, NULL);
}
```

`thread_yield()`는 현재 실행 중인 thread가 CPU를 양보하면서 다시 ready 상태가 되는 함수다. 이때도 단순히 리스트 뒤에 넣으면 priority 순서가 깨질 수 있으므로, `thread_unblock()`과 동일하게 `list_insert_ordered()`를 사용했다. `idle_thread`는 ready thread가 하나도 없을 때만 실행되어야 하므로 기존처럼 `ready_list`에 다시 넣지 않는다.

`next_thread_to_run()`은 기존 구조를 유지했다.

```c
return list_entry (list_pop_front (&ready_list), struct thread, elem);
```

변경 전에는 `ready_list`가 FIFO 순서였기 때문에 front가 가장 오래 기다린 thread를 의미했다. 변경 후에는 `ready_list`가 priority 내림차순으로 유지되므로, 같은 코드가 가장 높은 priority thread를 선택하는 역할을 한다. 즉, scheduler 선택 함수 자체를 복잡하게 바꾸지 않고, ready queue의 정렬 기준을 바꾸는 방식으로 priority scheduling을 구현했다.

`thread_create()`에서는 새로 생성된 thread가 현재 실행 중인 thread보다 priority가 높으면 즉시 CPU를 양보하도록 처리했다.

```c
thread_unblock (t);

if (t->priority > thread_current ()->priority) {
	thread_yield ();
}
```

새 thread는 `thread_unblock()`을 통해 `ready_list`에 들어간다. 이 thread의 priority가 현재 thread보다 높다면, 현재 thread가 계속 실행되는 것은 priority scheduling의 기대 동작과 맞지 않는다. 따라서 새 thread 생성 직후 priority를 비교하고, 새 thread가 더 높으면 `thread_yield()`를 호출해 scheduler가 높은 priority thread를 바로 선택할 수 있게 했다.

`thread_set_priority()`에서는 현재 thread의 priority를 바꾼 뒤, ready list의 가장 높은 priority thread와 비교하도록 수정했다.

```c
struct thread *curr = thread_current ();

curr->priority = new_priority;

if (!list_empty (&ready_list)) {
	struct thread *front = list_entry (list_front (&ready_list),
			struct thread, elem);

	if (front->priority > curr->priority) {
		thread_yield ();
	}
}
```

현재 thread가 priority를 낮추면 ready 상태의 다른 thread가 더 높은 priority를 가지게 될 수 있다. 이 경우 현재 thread가 계속 CPU를 점유하면 priority scheduling이 깨진다. 따라서 `ready_list`가 정렬되어 있다는 전제를 이용해 front만 확인하고, front thread의 priority가 현재 thread보다 높으면 즉시 `thread_yield()`를 호출한다.

## 3. 코드 리뷰 의사결정

### `thread_current()` 중복 호출 제거

코드 리뷰 과정에서 `thread_set_priority()` 안에서 현재 thread를 얻기 위해 `thread_current()`를 중복 호출하던 부분을 정리하기로 했다.

기존 흐름은 현재 thread의 priority를 직접 갱신한 뒤, 다시 `thread_current()`를 호출해 `curr` 변수에 저장하는 형태였다.

```c
thread_current ()->priority = new_priority;

struct thread *curr = thread_current ();
curr->priority = new_priority;
```

이 코드는 같은 thread에 대해 같은 priority 대입을 두 번 수행하고, `thread_current()`도 두 번 호출한다. 기능적으로는 큰 차이가 없지만, 코드 의도가 불명확하고 중복이 생긴다.

리뷰 후에는 현재 thread 포인터를 한 번만 얻고, 이후 로직에서는 `curr`를 사용하도록 정리하는 방향으로 결정했다.

```c
struct thread *curr = thread_current ();
curr->priority = new_priority;
```

이렇게 바꾸면 `thread_current()` 함수 호출이 한 번만 일어나고, 이후 priority 갱신과 ready list front 비교가 모두 같은 `curr` 기준으로 수행된다. 결과적으로 동작은 유지하면서 중복 코드를 줄이고, `thread_set_priority()`가 "현재 thread의 priority를 바꾸고 필요하면 양보한다"는 흐름이 더 명확해진다.

## 4. 테스트 확인 범위

이번 priority scheduling 구현의 주요 확인 대상은 다음 테스트들이다.

```text
alarm-priority
priority-change
priority-fifo
priority-preempt
```

각 테스트가 확인하는 핵심은 다음과 같다.

- `alarm-priority`: timer로 깨어난 thread들이 priority 순서대로 실행되는지 확인한다.
- `priority-change`: 현재 thread가 priority를 낮췄을 때 더 높은 priority thread에게 CPU를 양보하는지 확인한다.
- `priority-fifo`: 같은 priority를 가진 thread들이 FIFO 순서를 유지하는지 확인한다.
- `priority-preempt`: 더 높은 priority thread가 ready 상태가 되면 현재 thread를 선점하는지 확인한다.

전체 threads 테스트는 다음 명령으로 실행할 수 있다.

```bash
make -C pintos/threads/build check
```
