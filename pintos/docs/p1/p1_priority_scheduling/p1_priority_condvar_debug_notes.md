# Priority Sema/Condvar Debug Notes

## 배경

`priority-sema`와 `priority-condvar`를 디버깅하면서 확인한 내용을 정리한다. 핵심 주제는 `sema_up()` 이후 선점 처리, interrupt context, `cond_wait()`에서 사용하는 `semaphore_elem`, 그리고 Pintos의 intrusive list 구조다.

## 1. `thread_unblock()` 안에서 바로 yield하면 왜 위험한가

처음에는 `thread_unblock()`에서 새로 READY가 된 thread가 현재 thread보다 priority가 높으면 바로 `thread_yield()`를 호출하는 방식이 고려되었다.

하지만 `thread_unblock()`은 낮은 레벨 함수라서 호출자가 아직 자기 작업을 끝내지 않았을 수 있다. 예를 들어 `sema_up()` 내부에서 `thread_unblock()`을 호출한 직후 바로 yield하면, `sema_up()`의 나머지 상태 갱신이 끝나기 전에 깨운 thread가 실행될 수 있다.

대표적인 문제 흐름은 다음과 같다.

```text
1. sema_up()이 waiter thread를 unblock한다.
2. thread_unblock() 안에서 바로 yield가 발생한다.
3. 깨운 thread가 sema_down()으로 돌아온다.
4. 그런데 sema_up()의 sema->value++가 아직 실행되지 않았다.
5. 깨운 thread가 다시 sema->value == 0을 보고 block될 수 있다.
```

따라서 `thread_unblock()`은 thread를 READY 상태로 만들고 ready list에 넣는 역할에 집중하고, 선점 판단은 호출자가 안전한 시점에 하는 쪽이 자연스럽다.

## 2. `sema_up()`에서 선점 판단을 한다면 언제 해야 하는가

`sema_up()` 기준으로는 semaphore 내부 상태 변경이 끝난 뒤에 선점 판단을 해야 한다.

```text
1. interrupt를 끈다.
2. waiters에서 깨울 thread를 고른다.
3. thread_unblock()으로 ready_list에 넣는다.
4. sema->value를 증가시킨다.
5. interrupt level을 원래대로 복구한다.
6. 필요하면 현재 thread가 yield한다.
```

`intr_set_level(old_level)`은 무조건 interrupt를 켜는 함수가 아니라, 이전 interrupt level로 복구하는 함수다.

## 3. interrupt context에서는 왜 `thread_yield()`를 바로 부르면 안 되는가

interrupt handler 안에서 실행 중인지 확인하는 함수는 다음과 같다.

```c
intr_context()
```

일반 thread context에서는 `thread_yield()`를 직접 호출할 수 있다. 하지만 timer interrupt 같은 interrupt handler 안에서는 일반적인 thread 흐름이 아니므로 `thread_yield()`를 직접 호출하면 안 된다. `thread_yield()` 안에도 다음 ASSERT가 있다.

```c
ASSERT(!intr_context());
```

interrupt context에서 선점이 필요하면 다음 함수를 사용해 interrupt handler가 끝난 뒤 yield되도록 예약해야 한다.

```c
intr_yield_on_return();
```

개념적으로는 다음과 같이 나뉜다.

```text
일반 thread context:
  thread_yield()

interrupt context:
  intr_yield_on_return()
```

다만 `priority-sema` 테스트에서는 interrupt context 경로를 밟지 않아도 통과할 수 있다. 그래서 `intr_yield_on_return()` 없이도 해당 테스트만 통과할 수는 있다.

## 4. ready_list는 스케줄러가 다시 정렬하지 않는다

`next_thread_to_run()`은 ready list를 다시 정렬하지 않는다. ready list의 맨 앞 원소를 그대로 꺼낸다.

```c
static struct thread *
next_thread_to_run(void)
{
  if (list_empty(&ready_list))
    return idle_thread;
  else
    return list_entry(list_pop_front(&ready_list), struct thread, elem);
}
```

따라서 priority scheduling에서는 ready list에 넣는 시점에 정렬을 유지해야 한다.

```text
thread_unblock():
  ready_list에 unblock된 thread를 priority 순서로 삽입

thread_yield():
  현재 thread를 ready_list에 priority 순서로 삽입

next_thread_to_run():
  ready_list의 front를 pop
```

즉 스케줄러가 정렬해서 고르는 것이 아니라, ready list가 항상 정렬되어 있다는 invariant를 믿고 `pop_front()`만 수행한다.

## 5. `cond_wait()`에서 `condition.waiters`와 `semaphore.waiters`는 다르다

`cond_wait()`에서는 condition variable에서 기다리는 thread를 직접 condition의 waiters list에 넣지 않는다. 대신 `struct semaphore_elem`이라는 wrapper를 사용한다.

```c
struct semaphore_elem
{
  struct list_elem elem;
  struct semaphore semaphore;
  struct thread *thread;
};
```

`cond_wait()` 한 번 호출마다 지역 변수로 `semaphore_elem waiter`가 만들어진다.

```text
condition.waiters
  -> waiter.elem
       waiter.semaphore.waiters
         -> thread.elem
```

두 list element는 서로 다르다.

```text
waiter.elem:
  condition.waiters에 연결되는 노드

thread.elem:
  semaphore.waiters 또는 ready_list에 연결되는 thread의 노드
```

같은 `thread.elem`을 condition waiters와 semaphore waiters에 동시에 넣을 수는 없다. `list_elem`은 `prev`, `next` 포인터를 가진 연결 노드이므로 한 번에 하나의 list에만 속할 수 있다.

## 6. 하나의 `list_elem`을 두 리스트에 넣을 수 없는 이유

Pintos의 list는 intrusive list다. 즉 list가 데이터를 따로 감싸서 저장하는 것이 아니라, 각 구조체 안에 들어 있는 `struct list_elem`의 `prev`, `next` 포인터를 직접 연결한다.

```c
struct list_elem {
  struct list_elem *prev;
  struct list_elem *next;
};
```

어떤 `list_elem`이 하나의 list에 들어가면, 그 `prev`와 `next`는 해당 list 안에서 앞뒤 원소를 가리키도록 설정된다. 그런데 같은 `list_elem`을 다른 list에 다시 넣으면, 기존 `prev`, `next` 값이 새 list 기준으로 덮어써진다.

따라서 하나의 `thread.elem`을 `condition.waiters`와 `semaphore.waiters`에 동시에 넣으면 한쪽 list의 연결 정보가 깨진다. 이것은 단순한 설계 취향 문제가 아니라, 실제 포인터 연결이 덮어써져 list 구조가 망가지는 기술적인 문제다.

`cond_wait()`에서 `semaphore_elem.elem`을 따로 사용하는 이유가 바로 이것이다. condition variable은 "이 condition에서 기다리는 wait 호출들"을 `condition.waiters`에 보관해야 하고, 실제 thread는 개인 semaphore의 `semaphore.waiters`에서 block되어야 한다. 이 두 관계가 동시에 필요하므로 서로 다른 `list_elem`이 필요하다.

```text
condition.waiters
  -> semaphore_elem.elem

semaphore_elem.semaphore.waiters
  -> thread.elem
```

이 구조를 donation 구현에도 그대로 적용할 수 있다. lock을 기다리는 donor thread는 lock 내부 semaphore의 waiters에 `thread.elem`으로 들어가면서, 동시에 lock holder의 donations 리스트에도 등록되어야 한다. 그래서 donation 관계에는 기존 `thread.elem`이 아니라 별도의 `donation_elem`이 필요하다.

문제가 생기는 일반적인 경우는 "같은 객체가 동시에 두 리스트에 속해야 하는데 같은 `list_elem`을 재사용할 때"다. 예를 들어 semaphore waiters와 donation list에 동시에 들어가야 하거나, condition waiters와 개인 semaphore waiters에 동시에 들어가야 하는 경우가 여기에 해당한다.

## 7. `semaphore_elem`은 무엇을 의미하는가

`semaphore_elem`은 condition variable에서 기다리는 "wait 호출 하나"를 표현하는 wrapper다.

condition variable 자체는 thread를 직접 재우고 깨우는 기능을 갖지 않는다. Pintos의 기본 block/unblock 도구는 semaphore이므로, condition variable은 각 waiter마다 개인 semaphore를 만들어 그 semaphore에서 thread를 재운다.

흐름은 다음과 같다.

```text
1. thread A가 cond_wait()을 호출한다.
2. thread A의 kernel stack 위에 semaphore_elem waiter가 생긴다.
3. waiter.elem이 condition.waiters에 들어간다.
4. thread A는 waiter.semaphore에서 sema_down()으로 block된다.
5. 다른 thread가 cond_signal()을 호출한다.
6. condition.waiters에서 waiter 하나를 꺼낸다.
7. 그 waiter.semaphore에 sema_up()을 호출한다.
8. 해당 semaphore에서 block된 thread A가 깨어난다.
```

여기서 개인 semaphore는 공유 자원 개수를 세기 위한 용도라기보다, 해당 waiter 하나를 재우고 깨우는 0/1 신호 장치처럼 쓰인다.

## 8. `waiter.elem`은 왜 따로 초기화하지 않는가

`waiter.elem`은 다음 선언으로 `waiter` 안에 함께 생긴다.

```c
struct semaphore_elem waiter;
```

이때 메모리에는 다음 필드들이 존재한다.

```text
waiter.elem.prev
waiter.elem.next
waiter.semaphore.value
waiter.semaphore.waiters
waiter.thread
```

`list_elem`은 실제 데이터를 담는 값이 아니라 list에 연결되기 위한 노드다. `list_insert_ordered()`, `list_push_back()`, `list_push_front()` 같은 함수가 list에 삽입하면서 `prev`, `next`를 설정한다.

따라서 `waiter.elem` 자체를 별도로 초기화할 필요는 없다. 반대로 다음처럼 다른 list element를 복사하면 list 연결 정보가 꼬일 수 있다.

```c
waiter.elem = lock->holder->elem;
```

초기화가 필요한 것은 `waiter.semaphore`다.

```c
sema_init(&waiter.semaphore, 0);
```

그리고 비교 함수가 사용할 `waiter.thread`도 삽입 전에 세팅해야 한다.

```c
waiter.thread = thread_current();
```

## 9. `priority-condvar`에서 새 비교 함수가 필요한 이유

`sema->waiters`에는 `struct thread`의 `elem`이 들어간다.

```text
sema->waiters
  -> thread.elem
```

반면 `cond->waiters`에는 `struct semaphore_elem`의 `elem`이 들어간다.

```text
cond->waiters
  -> semaphore_elem.elem
```

따라서 `cond->waiters`를 정렬할 때 기존 `thread_priority_compare()`를 그대로 사용하면 안 된다. 그 함수는 list element를 `struct thread`로 해석하기 때문이다.

`cond->waiters`용 비교 함수는 list element를 `struct semaphore_elem`으로 해석해야 한다.

## 10. 기존 panic 원인

디버깅 중 다음 panic이 발생했다.

```text
PANIC at ../../lib/kernel/list.c:238 in list_front():
assertion `!list_empty (list)' failed.

list_front
-> semaphore_priority_compare
-> list_insert_ordered
-> cond_wait
```

원인은 `cond->waiters`가 비어있는지가 아니라, 새로 삽입하려는 `waiter.semaphore.waiters`가 아직 비어있다는 점이었다.

문제 흐름은 다음과 같다.

```text
1. sema_init(&waiter.semaphore, 0)
2. cond->waiters에 waiter.elem을 정렬 삽입하려고 함
3. 비교 함수가 waiter.semaphore.waiters의 front를 봄
4. 하지만 sema_down(&waiter.semaphore)을 아직 호출하지 않음
5. waiter.semaphore.waiters는 비어 있음
6. list_front(empty list)로 panic
```

즉 `cond_wait()`에서 `cond->waiters`에 삽입하는 시점에는 아직 현재 thread가 `waiter.semaphore.waiters`에 들어가기 전이다.

## 11. 이 문제를 고치는 방향

`cond->waiters`를 정렬하기 위해 `waiter.semaphore.waiters` 내부를 보면 안 된다. 삽입 시점에 그 list는 비어있을 수 있기 때문이다.

따라서 condition waiter의 priority 기준값은 `cond->waiters`에 삽입하기 전에 알 수 있는 값이어야 한다.

처음에는 priority 값을 직접 저장하는 방식을 생각할 수 있다.

```text
cond_wait() 진입 시점의 current thread priority를 waiter에 저장
cond->waiters 비교 함수는 저장된 priority를 기준으로 비교
```

다만 나중에 priority donation을 구현하면 저장된 priority 값이 stale value가 될 수 있다.

따라서 donation까지 고려하면 `int priority`를 복사해 두는 방식보다 `struct thread *thread`를 저장하고, 비교 시점에 해당 thread의 현재 priority를 읽는 방식이 더 자연스럽다.

```text
priority 값을 복사하는 방식:
  구현은 단순하지만 donation 이후 값 갱신 문제가 생긴다.

thread 포인터를 저장하는 방식:
  thread 구조체의 현재 priority를 직접 읽을 수 있다.
  다만 priority가 바뀌었을 때 이미 정렬된 list의 순서는 자동으로 바뀌지 않으므로,
  donation 단계에서는 waiters list 재정렬 정책을 별도로 정해야 한다.
```

## 11. 추가로 확인해야 할 invariant

현재 구현에서 함께 확인해야 할 점은 다음과 같다.

```text
thread_unblock():
  ready_list에 넣은 thread의 status가 THREAD_READY로 바뀌는가

sema_up():
  waiter가 없는 경우에도 무조건 yield하지는 않는가
  interrupt context에서는 intr_yield_on_return()을 실제로 호출하는가

cond_wait():
  cond->waiters에는 semaphore_elem.elem이 들어가는가
  lock release -> sema_down -> lock acquire 순서가 유지되는가

cond_signal():
  cond->waiters에서 pop을 한 번만 하는가
  priority가 가장 높은 waiter를 깨우는가
```

## 12. `list_insert_ordered()`는 무엇을 보고 삽입 위치를 찾는가

`list_insert_ordered()`의 두 번째 인자는 "값"이 아니라 리스트에 연결할 `struct list_elem`의 주소다. `list_elem` 자체에는 priority 같은 정렬 값이 들어 있지 않다.

```c
struct list_elem {
  struct list_elem *prev;
  struct list_elem *next;
};
```

따라서 삽입 위치를 찾을 때는 비교 함수가 `list_elem`을 바깥 구조체로 복원한 뒤, 그 구조체 안의 값을 읽는다.

`ready_list`의 경우 흐름은 다음과 같다.

```text
&thread.elem
  -> list_entry(elem, struct thread, elem)
  -> struct thread 전체를 찾음
  -> thread.priority를 비교
```

`cond->waiters`의 경우 흐름은 다음과 같다.

```text
&waiter.elem
  -> list_entry(elem, struct semaphore_elem, elem)
  -> struct semaphore_elem 전체를 찾음
  -> waiter.thread->priority를 비교
```

즉 `waiter.elem` 안에 priority가 들어 있어야 하는 것이 아니다. `waiter.elem`은 바깥 구조체를 다시 찾기 위한 연결 노드이고, 비교에 필요한 값은 바깥 구조체인 `semaphore_elem`이 들고 있어야 한다.

## 13. 기존 `semaphore_priority_compare()`의 마지막 두 줄이 의미한 것

디버깅 중 다음 비교 함수가 사용되었다.

```c
const struct thread *ta =
    list_entry(list_front(&sa->semaphore.waiters), struct thread, elem);
const struct thread *tb =
    list_entry(list_front(&sb->semaphore.waiters), struct thread, elem);
```

이 코드는 다음 구조를 가정한다.

```text
semaphore_elem
  -> semaphore.waiters
       -> thread.elem
```

즉 `semaphore_elem` 안의 개인 semaphore에서 기다리는 thread를 꺼내고, 그 thread의 priority를 비교하겠다는 뜻이다.

문제는 `cond_wait()`에서 `cond->waiters`에 `waiter.elem`을 삽입하는 시점에는 아직 `sema_down(&waiter.semaphore)`이 실행되지 않았다는 점이다.

```text
sema_init(&waiter.semaphore, 0)
list_insert_ordered(&cond->waiters, &waiter.elem, ...)
lock_release(lock)
sema_down(&waiter.semaphore)
```

따라서 비교 함수가 실행되는 시점에는 새로 들어오는 `waiter.semaphore.waiters`가 비어 있다. 이 상태에서 `list_front(&sa->semaphore.waiters)`를 호출하면 `list_front()`의 `!list_empty(list)` ASSERT가 실패한다.

## 14. `cond->waiters` 삽입을 `sema_down()` 뒤로 옮기면 안 되는 이유

`sema_down(&waiter.semaphore)` 뒤에 `list_insert_ordered(&cond->waiters, &waiter.elem, ...)`를 호출하면 내부 semaphore waiters가 채워질 것처럼 보일 수 있다.

하지만 `waiter.semaphore.value`는 0으로 초기화되어 있으므로, `sema_down()`을 호출하는 순간 현재 thread는 block된다.

```text
1. sema_down(&waiter.semaphore)를 호출한다.
2. value가 0이므로 현재 thread가 block된다.
3. 아직 cond->waiters에는 등록되지 않았다.
4. cond_signal()이 이 waiter를 찾을 방법이 없다.
5. 따라서 깨어날 수 없다.
```

condition waiters에 먼저 등록하고, 그 다음 lock을 release한 뒤 개인 semaphore에서 block되어야 한다.

## 15. `sema_up()`의 불필요한 context switch

`sema_up()`에서 waiter를 깨웠다고 해서 항상 `thread_yield()`가 필요한 것은 아니다.

불필요한 yield가 되는 경우는 다음과 같다.

```text
1. sema->waiters가 비어 있어서 실제로 깨운 thread가 없는 경우
2. 깨운 thread의 priority가 현재 thread보다 낮거나 같은 경우
3. ready_list front가 현재 thread보다 높지 않은 경우
```

priority scheduling에서 필요한 양보 조건은 다음 중 하나로 표현할 수 있다.

```text
방금 unblock한 thread의 priority > 현재 thread의 priority
```

또는 더 일반적으로:

```text
ready_list front의 priority > 현재 thread의 priority
```

따라서 `sema_up()`에서 조건 없이 yield하면 테스트는 통과할 수 있어도 불필요한 context switch가 발생한다.

## 16. `cond_wait()` 마지막의 `thread_yield()`가 필요 없는 이유

`cond_wait()`는 signal을 받아 깨어난 뒤 lock을 다시 획득하고 반환한다.

```text
sema_down(&waiter.semaphore)에서 깨어남
lock_acquire(lock)
cond_wait() 반환
```

이 시점에 `thread_yield()`를 호출하면 lock을 잡은 상태로 CPU를 양보하게 된다. 다른 thread가 같은 lock을 필요로 한다면 오히려 진행을 막을 수 있고, condition variable의 기본 흐름에도 필요하지 않다.

따라서 priority 선점 판단은 `cond_wait()` 마지막이 아니라, 더 높은 priority thread가 READY 상태가 되는 지점인 `sema_up()`, `thread_create()`, `thread_set_priority()` 같은 곳에서 처리하는 편이 자연스럽다.

## 요약

핵심은 `condition.waiters`에 들어가는 것이 thread 자체가 아니라 `semaphore_elem`이라는 wrapper라는 점이다. `semaphore_elem`은 condition wait 호출 하나를 나타내며, 그 안의 개인 semaphore가 실제 thread를 block/unblock한다.

`priority-condvar`에서 중요한 구조는 다음 한 줄로 정리할 수 있다.

```text
condition.waiters는 semaphore_elem들의 list이고,
각 semaphore_elem.semaphore.waiters는 실제 block된 thread들의 list다.
```

따라서 `cond->waiters` 정렬 기준은 `struct semaphore_elem` 관점에서 설계해야 하며, 삽입 시점에 비어있는 내부 semaphore waiters를 비교 기준으로 사용하면 안 된다.
