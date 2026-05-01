# PintOS Project 1 - Alarm Clock 구현 기록

## 1. 맥락

이번 구현에서는 기존 `timer_sleep()`의 busy waiting 구조를 제거하고, 잠들어야 하는 스레드를 `sleep_list`에 넣은 뒤 `thread_block()`으로 실제 block 상태에 두는 Alarm Clock 기반을 마련했다.

기존 방식은 `timer_sleep()` 안에서 시간이 지났는지 계속 확인하면서 `thread_yield()`를 호출하는 구조였기 때문에, CPU를 불필요하게 사용하고 busy waiting 원칙을 위반할 수 있었다. 이번 구현에서는 각 스레드가 깨어날 수 있는 절대 tick 값을 `wakeup_ticks`로 저장하고, timer interrupt가 매 tick마다 `sleep_list`의 앞쪽부터 확인해 시간이 된 스레드만 깨우도록 변경했다.

이 기반 위에서 PintOS는 여러 스레드가 서로 다른 시간 동안 잠들어도 CPU를 낭비하지 않고 관리할 수 있다. 또한 이후 Priority Scheduling, Priority Donation, MLFQS를 구현할 때도 timer interrupt와 scheduler의 관계, interrupt context에서 가능한 작업 범위, shared list를 다룰 때의 interrupt 제어 방식에 대한 이해를 바탕으로 확장할 수 있다.

이번 Alarm Clock 구현의 기준 커밋은 `55a112e Clean start`이고, 브랜치의 최종 커밋은 `a89adb2 fix(timer): removed while loop in timer_sleep()`이다. 중간 커밋은 `e675544 feat(timer): handle non-positive alarm sleep requests`, `e6abcad feat(timer): add blocked sleep list wake-up path` 순서로 진행되었다.

## 2. 코드 수정한 부분

### `pintos/include/threads/thread.h`

`struct thread`에 Alarm Clock 전용 필드인 `wakeup_ticks`를 추가했다.

```c
int64_t wakeup_ticks;  /* 잠든 스레드가 깨어날 수 있는 최소 절대 tick. */
```

이 필드는 `timer_sleep()`을 호출한 스레드가 언제부터 unblock 가능해지는지를 저장한다. semaphore나 lock 때문에 block된 모든 스레드에 대한 일반 필드라기보다는, Alarm Clock에서 sleep 상태로 들어간 스레드의 깨울 시각을 기록하는 용도다.

### `pintos/devices/timer.c`

처음 코드의 `timer_sleep()`은 다음처럼 `timer_elapsed()`를 반복 확인하면서 `thread_yield()`를 호출했다.

```c
void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks ();

	ASSERT (intr_get_level () == INTR_ON);
	while (timer_elapsed (start) < ticks)
		thread_yield ();
}
```

이 방식은 현재 스레드가 실제로 block되는 것이 아니라, 시간이 지났는지 계속 확인하면서 CPU를 양보하는 구조다. 따라서 Alarm Clock의 목표인 busy waiting 제거를 만족하지 못한다.

이를 바꾸기 위해 `sleep_list`를 추가했다.

```c
/* Blocked 스레드들이 대기하고 있는 공간 */
static struct list sleep_list;
```

`sleep_list`는 `timer_sleep()`에서 잠든 스레드들을 보관하는 리스트다. `wakeup_ticks` 오름차순으로 유지되므로, `timer_interrupt()`에서는 리스트의 front만 확인해도 아직 깰 수 없는 스레드가 있는지 빠르게 판단할 수 있다.

`timer_init()`에서 `sleep_list`를 초기화했다.

```c
/* sleep_list 초기화 */
list_init(&sleep_list);
```

`wakeup_ticks_less()` 비교 함수를 추가했다.

```c
/* 스레드 a의 wakeup_ticks가 스레드 b의 wakeup_ticks보다 빠를 경우 true 반환, 같거나 느리면 false 반환.
	 첫 번째, 두 번째 인자로 스레드 구조체 멤버인 elem을 받기 떄문에, 함수 내에서 thread로 변환하는 과정 필요. */
static bool wakeup_ticks_less(struct list_elem *a, struct list_elem *b, void *aux UNUSED) {
	struct thread *thread_a = list_entry(a, struct thread, elem);
	struct thread *thread_b = list_entry(b, struct thread, elem);

	return (thread_a->wakeup_ticks < thread_b->wakeup_ticks);
}
```

이 함수는 `list_insert_ordered()`의 세 번째 인자로 전달되는 정렬 기준 함수다. `struct list_elem *`를 직접 비교하는 것이 아니라, `list_entry()`로 원래의 `struct thread *`를 꺼낸 뒤 각 thread의 `wakeup_ticks`를 비교한다.

`timer_sleep()`을 busy waiting 방식에서 block 기반 방식으로 변경했다.

```c
/* ticks가 0 이하면 아무것도 반환하지 않고 함수 종료 */ 
if (ticks <= 0) {
	return;
}
/* 현재 타이머 tick값 저장: 기준 시간 */ 
int64_t start = timer_ticks ();
/* 스레드가 unblocked 될 수 있는 절대 시각 */
int64_t wakeup_ticks = start + ticks;
/* 현재 스레드의 포인터 저장 */
struct thread *current_thread = thread_current();
/* 현재 스레드의 구조체 멤버인 wakeup_ticks 갱신 */
current_thread->wakeup_ticks = wakeup_ticks;
/* 인터럽트 활성화 되어 있는 것을 ASSERT로 확인, 비활성화 상태라면 스레드를 UNBLOCKED 해줄 interrupt가 발생하지 않음 */
ASSERT (intr_get_level () == INTR_ON);
/* sleep_list에 스레드 삽입하고 block 처리 하는 동안 인터럽트 비활성화 해주고, 이전 상태(활성화)를 이후 상태 복구를 위해 old_level 변수에 저장.
	 왜 비활성화가 필요할까? -> sleep_list는 타이머 인터럽트 핸들러도 공유하는 shared data structure 이기 때문. 핸들러는 스레드가 아니라 lock 방식의 동기화가 불가능. */
enum intr_level old_level = intr_disable();
/* 3번째 인자인 정렬 기준 함수에 따라 스레드를 리스트에 삽입. */
list_insert_ordered(&sleep_list, &current_thread->elem, &wakeup_ticks_less, NULL);
/* sleep_list 삽입 이후 현재 thread를 block */
thread_block();
/* 인터럽트 활성화, 이전 상태를 복구. */
intr_set_level(old_level);
```

`ticks <= 0`이면 잠들 필요가 없으므로 바로 반환한다. 양수 tick에 대해서는 현재 스레드의 `wakeup_ticks`를 계산한 뒤, interrupt를 끄고 `sleep_list`에 정렬 삽입한다. 이후 현재 스레드를 `thread_block()`으로 block 상태로 만들고, 이전 interrupt level을 복구한다.

`timer_interrupt()`에서 시간이 된 스레드를 깨우는 로직을 추가했다.

```c
/* 타이머 인터럽트 핸들러. 타이머 하드웨어에 의해 일정 주기마다 실행됨. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	/* sleep_list가 empty 될 때 까지 반복 실행.
		 따라서 sleep_list에 blocked 상태인 스레드 모두를 unblock 가능. */
	while (!list_empty(&sleep_list)) {
		/* sleep_list 맨 앞에 있는 elem을 thread로 변환하고 그 포인터를 t에 저장. */
		struct thread *t = list_entry(list_front(&sleep_list), struct thread, elem);
		/* 만약 확인한 스레드의 wakeup_ticks가 아직 만료되지 않았다면 이번 실행에서는 할 일이 없으니 break. */
		if (t->wakeup_ticks > ticks) {
			break;
		}
		/* break 걸리지 않았다면 현재 스레드 wakeup_ticks 만료, sleep_list에서 제거 & unblock */
		list_pop_front(&sleep_list);
		thread_unblock(t);
	}
	/* 매 tick 마다 스케줄러 통계 갱신 + time slice 선점 여부 판단 */
	thread_tick ();
}
```

timer interrupt는 매 tick마다 실행된다. `sleep_list`가 `wakeup_ticks` 오름차순으로 정렬되어 있으므로, front의 스레드가 아직 깰 시간이 아니라면 그 뒤의 스레드들도 깰 시간이 아니라고 판단하고 반복을 중단한다. 반대로 front의 `wakeup_ticks`가 현재 `ticks` 이하이면 리스트에서 제거하고 `thread_unblock()`으로 ready 상태로 되돌린다.

## 3. 구현하면서 어려웠던 부분 및 해결 과정

### `list_insert_ordered()`의 세 번째 인자인 `list_less_func` 이해

처음에는 `list_insert_ordered()`의 세 번째 인자가 정확히 어떤 값을 요구하는지 헷갈렸다. `wakeup_ticks` 기준으로 정렬해야 하므로 숫자나 필드 이름을 넘기면 될 것처럼 보였지만, 실제 인자 타입은 `list_less_func *`였다.

`list_less_func`는 리스트 원소 두 개를 비교하는 함수 타입이다. 따라서 `sleep_list`에 들어가는 각 원소의 순서를 판단하려면, `wakeup_ticks` 값을 직접 넘기는 것이 아니라 두 `struct list_elem *`를 받아 어느 쪽이 앞에 와야 하는지 판단하는 함수를 만들어야 했다.

해결 과정에서는 `list_insert_ordered()`가 내부에서 `less(elem, e, aux)` 형태로 비교 함수를 호출한다는 점을 확인했다. 이 구조를 이해한 뒤, `wakeup_ticks_less()`라는 비교 함수를 만들고 이 함수 이름을 세 번째 인자로 넘기는 방식이 PintOS list API를 의도대로 사용하는 방법이라는 점을 정리했다.

결론적으로 어려웠던 부분은 정렬 알고리즘 자체가 아니었다. C에서 함수 포인터를 인자로 넘기는 방식, 그리고 PintOS list가 비교 기준을 함수로 주입받는 구조에 익숙하지 않았던 것이 핵심이었다.

### `aux`에 `wakeup_ticks`를 넘기면 되는지에 대한 오해

처음에는 `list_less_func`의 세 번째 인자인 `aux`에 `wakeup_ticks` 값을 넣으면 정렬 기준을 전달할 수 있지 않을까 생각했다. 하지만 `aux`는 비교 함수 자체를 대체하는 자리가 아니라, 비교 함수가 필요할 때 참고할 수 있는 추가 데이터다.

정렬 삽입에서는 매번 새 원소와 기존 원소를 비교해야 한다. 즉 필요한 판단은 "현재 thread의 wakeup_ticks 하나"가 아니라 "두 thread의 wakeup_ticks 중 어느 쪽이 더 작은가"이다.

따라서 `aux`에 어떤 하나의 tick 값을 넣는 방식으로는 문제를 해결할 수 없다. `list_insert_ordered()`는 반드시 두 `list_elem`을 비교하는 `less` 함수를 호출하고, `aux`는 그 함수에 부가적으로 전달되는 값일 뿐이다.

이번 구현에서는 비교에 필요한 모든 정보가 각 `struct thread` 안의 `wakeup_ticks`에 이미 들어 있다. 그래서 `aux`는 사용하지 않고, 함수 시그니처를 맞추기 위해 `void *aux UNUSED` 형태로만 받도록 했다.

### 기존 `while (timer_elapsed(start) < ticks)` 구조가 왜 문제인지 이해

기존 `timer_sleep()`의 `while (timer_elapsed(start) < ticks)`는 겉으로 보면 정해진 시간이 지날 때까지 기다리는 자연스러운 코드처럼 보였다. `timer_elapsed(start)`는 기준 시각부터 지금까지 흐른 tick 수이고, `ticks`는 최소로 기다려야 하는 시간이다.

하지만 이 구조는 스레드가 실제로 잠드는 것이 아니라, 깨어 있는 상태에서 시간이 지났는지 계속 확인하는 방식이다. 기존 코드에서는 조건이 참인 동안 `thread_yield()`를 호출했지만, 여전히 주기적으로 다시 실행되어 시간을 검사해야 한다.

Alarm Clock의 목표는 이런 반복 감시를 없애는 것이다. 스레드는 한 번 `sleep_list`에 들어간 뒤 `thread_block()`으로 block되어야 하고, 이후 시간이 되었는지 확인하는 책임은 timer interrupt 쪽으로 옮겨져야 한다.

이 부분을 확인하면서 `timer_sleep()`에서 `while`을 제거해야 한다는 결론을 냈다. 현재 스레드는 `sleep_list`에 한 번만 삽입되고, 한 번만 block되어야 한다. 반복 확인은 `timer_interrupt()`가 매 tick마다 수행한다.

### `timer_interrupt()`가 누가 실행하는 함수인지 이해

`timer_interrupt()`는 우리가 직접 호출하는 일반 함수처럼 보였지만, 실제로는 하드웨어 timer interrupt가 발생할 때 PintOS interrupt 처리 흐름을 통해 호출된다. `timer_init()`에서 `intr_register_ext(0x20, timer_interrupt, "8254 Timer")`로 등록해 두면, 0x20번 외부 인터럽트가 발생할 때 이 함수가 실행된다.

즉 실행 흐름은 현재 실행 중인 스레드가 직접 `timer_interrupt()`를 부르는 방식이 아니다. 8254 PIT timer hardware가 일정 주기로 interrupt를 발생시키고, CPU가 현재 실행 흐름을 잠시 멈춘 뒤 PintOS의 interrupt dispatch 경로를 통해 등록된 handler를 호출한다.

이 구조를 이해하면서 `timer_interrupt()`가 왜 매 tick마다 `ticks++`를 수행할 수 있는지, 그리고 왜 `sleep_list`의 front를 확인해 시간이 된 스레드를 깨우는 위치로 적절한지 정리할 수 있었다.

또한 이 함수는 interrupt context에서 실행되기 때문에 일반 thread context와 같은 방식으로 생각하면 안 된다. 오래 걸리는 작업이나 sleep/block 가능한 작업은 피해야 하고, 이번 구현에서는 block된 thread를 ready 상태로 되돌리는 `thread_unblock()`만 수행하도록 했다.

### `sleep_list`의 front 확인/삭제 helper 확인

처음에는 `sleep_list`의 front가 만료되었는지 확인하는 helper와 front를 pop하는 helper를 새로 만들어야 하는지 고민했다. 이후 `pintos/include/lib/kernel/list.h`와 `pintos/lib/kernel/list.c`를 직접 확인하면서 기본 list API에 이미 필요한 함수들이 있다는 점을 확인했다.

front 원소를 확인하는 함수는 `list_front()`이고, front 원소를 제거하면서 가져오는 함수는 `list_pop_front()`였다. 또한 리스트가 비었는지 확인하는 함수는 `list_empty()`이다.

다만 이 함수들은 `sleep_list`의 의미를 알지 못한다. 즉 `list_front()`는 단지 맨 앞의 `struct list_elem *`를 돌려줄 뿐이고, 그 원소의 `wakeup_ticks`가 현재 tick 기준으로 만료되었는지는 판단하지 않는다.

그래서 해결 방식은 list API를 그대로 사용하되, timer-specific한 판단은 `timer_interrupt()` 안에서 수행하는 것이었다. `list_front()`로 front elem을 가져오고, `list_entry()`로 `struct thread *`를 복원한 뒤, `t->wakeup_ticks > ticks`인지 확인하도록 정리했다.

## 4. 처음 알게된 개념

### PintOS list와 `list_entry()`

PintOS의 list는 `struct thread *` 같은 구체적인 타입을 직접 저장하지 않는다. 대신 각 구조체 안에 `struct list_elem elem`을 멤버로 넣어두고, 리스트에는 이 `elem`이 연결된다.

그래서 `sleep_list`에서 꺼낸 값도 처음에는 `struct thread *`가 아니라 `struct list_elem *`이다. 이 값을 실제 thread로 사용하려면 `list_entry()` 매크로를 통해 해당 `list_elem`을 포함하고 있는 바깥 구조체를 찾아야 한다.

이번 구현에서는 `list_entry(a, struct thread, elem)` 형태를 사용해 비교 함수 안에서 `struct list_elem *`를 `struct thread *`로 되돌렸다. 그리고 그 결과로 얻은 thread의 `wakeup_ticks` 값을 비교했다.

이 방식은 처음에는 우회적으로 느껴졌지만, PintOS list가 여러 타입의 구조체를 하나의 list 구현으로 다루기 위한 방식이라는 점을 알게 되었다. 즉 list는 원소의 실제 타입을 모르고, 사용하는 쪽에서 `list_entry()`로 원래 타입을 복원한다.

### `aux` 인자의 역할

`aux`는 비교 함수가 필요할 때 참고할 수 있도록 같이 넘겨주는 추가 데이터다. 하지만 모든 비교 함수가 `aux`를 사용해야 하는 것은 아니다.

`list_insert_ordered()`는 정렬 기준 함수의 시그니처를 `list_less_func`로 고정해 두었기 때문에, 비교 함수는 항상 `const struct list_elem *a`, `const struct list_elem *b`, `void *aux` 형태의 인자를 받아야 한다.

이번 구현에서는 두 thread의 `wakeup_ticks`만 비교하면 되므로 외부에서 추가로 참고할 값이 필요하지 않았다. 그래서 `aux`는 로직에서 사용하지 않았고, 사용하지 않는 인자임을 표시하기 위해 `UNUSED`를 붙였다.

중요한 점은 `aux`가 정렬 기준 함수 자체를 대신할 수 없다는 것이다. 실제 순서 판단은 반드시 `less(a, b, aux)` 형태로 호출되는 비교 함수 안에서 수행되어야 하고, `aux`는 그 판단을 도와주는 선택적 부가 정보일 뿐이다.

### Interrupt context에서는 sleep/block 가능한 함수를 호출하면 안 됨

`timer_interrupt()`는 일반 thread 함수가 아니라 interrupt context에서 실행된다. 하드웨어 interrupt가 들어오면 CPU가 현재 흐름을 멈추고 handler를 실행하는 구조이기 때문에, 이 안에서는 일반적인 blocking 동작을 수행하면 안 된다.

예를 들어 interrupt handler 안에서 `thread_block()`처럼 현재 실행 흐름을 잠들게 하는 함수를 호출하면 스케줄링 상태가 깨질 수 있다. interrupt handler는 짧고 예측 가능하게 끝나야 하며, 잠들어서 다른 event를 기다리는 방식으로 동작하면 안 된다.

이번 구현에서 `timer_interrupt()`는 `sleep_list`에서 시간이 된 스레드를 꺼내 `thread_unblock()`만 호출한다. 즉 interrupt handler 자신이 잠드는 것이 아니라, block되어 있던 다른 thread를 ready 상태로 옮기는 역할만 한다.

이 개념을 이해하면서 `timer_sleep()`과 `timer_interrupt()`의 책임을 분리할 수 있었다. `timer_sleep()`은 thread context에서 현재 스레드를 block하고, `timer_interrupt()`는 interrupt context에서 시간이 된 스레드를 깨우는 쪽만 담당한다.
