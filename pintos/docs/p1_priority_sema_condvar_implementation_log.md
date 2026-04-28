# PintOS Project 1 - Priority Sema/Condvar 구현 기록

## 1. 맥락

이번 구현에서는 기본 priority scheduling 위에 semaphore와 condition variable의 priority-aware 동작을 추가했다. 기존 priority scheduling은 `ready_list`를 priority 내림차순으로 유지하고, `thread_create()`, `thread_yield()`, `thread_set_priority()`에서 선점 여부를 판단하는 구조였다.

하지만 semaphore와 condition variable은 thread가 BLOCKED 상태로 들어갔다가 다시 READY 상태로 돌아오는 핵심 경로다. 따라서 `sema_down()`에서 기다리는 thread들의 순서, `sema_up()`에서 깨울 thread 선택, `cond_signal()`에서 condition waiter를 깨우는 순서가 priority 기준을 따라야 `priority-sema`, `priority-condvar` 테스트를 통과할 수 있다.

이번 변경은 `feature/p1-priority-scheduling` 브랜치의 다음 구현 커밋들을 통해 진행되었다.

```text
e3e4c6f fix(priority): thread_unblock() to pass the sema test, cond_wait() and added new comparison func to pass the condvar test
4a3169b fix(priority): Fix priority scheduling in semaphores and condition variables
ca1ad72 fix(priority): update current scheduling changes (sema, condition variable
6a8306d feat(priority): Implemented priority scheduling
```

## 2. 코드 수정한 부분

### `pintos/threads/synch.c`

`sema_down()`에서는 semaphore waiters에 현재 thread를 단순 FIFO로 넣지 않고 priority 순서로 삽입하도록 변경했다.

```c
list_insert_ordered(&sema->waiters, &thread_current()->elem,
                    thread_priority_compare, NULL);
```

`sema->waiters`는 `struct thread`의 `elem`을 담는 리스트다. 따라서 `thread_priority_compare()`를 그대로 사용할 수 있다. 이 변경으로 여러 thread가 같은 semaphore에서 block되었을 때, `sema_up()`은 가장 높은 priority thread를 먼저 깨울 수 있다.

`sema_up()`에서는 waiters의 front를 꺼내 `thread_unblock()`으로 READY 상태로 전환한다. 이후 `sema->value` 증가와 interrupt level 복구까지 끝낸 뒤, 실제로 깨운 thread가 현재 thread보다 priority가 높은 경우에만 선점 처리를 수행한다.

```c
if (t != NULL && t->priority > thread_current()->priority) {
  if (!intr_context())
    thread_yield();
  else
    intr_yield_on_return();
}
```

초기에는 `thread_unblock()` 안에서 바로 yield하는 방향도 고려했지만, 그 경우 `sema_up()`의 상태 갱신이 끝나기 전에 깨운 thread가 실행될 수 있다. 최종 구현에서는 semaphore 상태 변경을 먼저 완료하고, 호출 문맥에 따라 일반 thread context에서는 `thread_yield()`, interrupt context에서는 `intr_yield_on_return()`을 사용하도록 정리했다.

`struct semaphore_elem`에는 condition waiter에 대응되는 thread를 저장하는 포인터를 추가했다.

```c
struct semaphore_elem
{
  struct list_elem elem;
  struct semaphore semaphore;
  struct thread *thread;
};
```

condition variable의 `waiters` 리스트에는 `struct thread`가 직접 들어가지 않는다. `cond_wait()` 호출마다 만들어지는 `struct semaphore_elem`의 `elem`이 들어간다. 따라서 condition waiters를 정렬하려면 `struct semaphore_elem`을 기준으로 비교해야 한다.

이를 위해 `semaphore_priority_compare()`를 추가했다.

```c
return sa->thread->priority > sb->thread->priority;
```

비교 함수가 `semaphore.waiters` 내부를 보지 않고 `semaphore_elem.thread`를 보는 이유는 삽입 시점 때문이다. `cond_wait()`에서 `cond->waiters`에 `waiter.elem`을 넣는 시점에는 아직 `sema_down(&waiter.semaphore)`이 실행되지 않았으므로, `waiter.semaphore.waiters`는 비어 있을 수 있다. 따라서 삽입 전에 알 수 있는 현재 thread 포인터를 저장해 비교 기준으로 사용했다.

`cond_wait()`에서는 다음 순서를 유지했다.

```text
1. 개인 semaphore를 value 0으로 초기화한다.
2. 현재 thread 포인터를 semaphore_elem에 저장한다.
3. semaphore_elem.elem을 condition waiters에 priority 순서로 넣는다.
4. lock을 release한다.
5. 개인 semaphore에서 sema_down()으로 block된다.
6. signal을 받아 깨어나면 lock을 다시 acquire한다.
```

이 구조에서 `cond->waiters`는 "condition에서 기다리는 waiter wrapper들의 리스트"이고, 각 wrapper 안의 개인 semaphore가 실제 thread를 block/unblock한다.

`cond_signal()`에서는 `cond->waiters`에서 priority가 가장 높은 `semaphore_elem`을 하나 꺼낸 뒤, 그 내부 semaphore에 `sema_up()`을 호출하도록 정리했다.

```c
struct semaphore_elem *waiter = list_entry(list_pop_front(&cond->waiters),
                                           struct semaphore_elem, elem);
sema_up(&waiter->semaphore);
```

기존 디버깅 과정에서 `list_pop_front()`가 중복 호출되면 signal 대상이 아닌 waiter까지 리스트에서 제거될 수 있었기 때문에, 최종 구현에서는 하나의 waiter만 pop하도록 했다.

### `pintos/threads/thread.c`

`thread_priority_compare()`는 `ready_list`뿐 아니라 semaphore waiters에서도 사용해야 하므로 파일 내부 `static` 함수가 아니라 외부에서 참조 가능한 함수로 변경했다.

```c
bool thread_priority_compare(const struct list_elem *a,
                             const struct list_elem *b,
                             void *aux UNUSED)
```

`thread_unblock()`은 BLOCKED thread를 READY 상태로 만들고 `ready_list`에 priority 순서로 삽입한다.

```c
list_insert_ordered(&ready_list, &t->elem, thread_priority_compare, NULL);
t->status = THREAD_READY;
```

선점 판단은 `thread_unblock()` 내부에서 하지 않는다. `thread_unblock()`은 semaphore, lock, condition variable, timer interrupt 등 다양한 문맥에서 호출될 수 있으므로, 호출자가 자기 자료구조 갱신을 마친 뒤 안전한 시점에 양보 여부를 판단하는 구조가 더 안정적이다.

### `pintos/include/lib/kernel/list.h`

`synch.c`와 `timer.c`에서도 priority 및 wakeup tick 비교 함수를 사용할 수 있도록 비교 함수 선언을 추가했다.

```c
bool thread_priority_compare(const struct list_elem *a,
                             const struct list_elem *b,
                             void *aux);
bool wakeup_ticks_less(const struct list_elem *a,
                       const struct list_elem *b,
                       void *aux);
```

## 3. 주요 의사결정

### `thread_unblock()`은 선점하지 않는다

`thread_unblock()` 안에서 바로 `thread_yield()`를 호출하면, `sema_up()`이나 `cond_signal()` 같은 호출자가 아직 내부 상태를 완성하기 전에 context switch가 발생할 수 있다.

따라서 `thread_unblock()`은 다음 역할만 담당하도록 유지했다.

```text
BLOCKED thread를 READY 상태로 바꾼다.
ready_list에 priority 순서로 삽입한다.
```

선점 판단은 `sema_up()`, `thread_create()`, `thread_set_priority()`처럼 READY thread를 만든 쪽에서 수행한다.

### condition waiters에는 `thread.elem`을 넣지 않는다

`cond->waiters`는 `struct semaphore_elem`의 리스트다. `struct thread`의 `elem`은 `ready_list`나 semaphore waiters에 사용되는 연결 노드이며, 같은 `list_elem`을 여러 리스트에 동시에 넣을 수 없다.

따라서 `cond_wait()`에서는 `waiter.elem`을 condition waiters에 넣고, 실제 thread는 `waiter.semaphore.waiters`에서 block되도록 유지했다.

### condition waiters 정렬 기준은 저장한 thread 포인터로 잡는다

`cond->waiters`에 `waiter.elem`을 삽입하는 시점에는 `waiter.semaphore.waiters`가 아직 비어 있다. 이 리스트의 front thread를 기준으로 비교하면 `list_front()` ASSERT가 발생할 수 있다.

최종 구현에서는 `waiter.thread = thread_current()`로 현재 thread를 저장하고, 이 포인터가 가리키는 thread의 priority를 비교 기준으로 사용했다.

## 4. 테스트 확인 범위

이번 구현의 직접 확인 대상은 다음 테스트들이다.

```text
priority-sema
priority-condvar
```

관련 priority scheduling 회귀 확인 대상으로는 다음 테스트들도 함께 확인할 수 있다.

```text
alarm-priority
priority-change
priority-fifo
priority-preempt
```

개별 테스트 실행 명령은 다음과 같다.

```bash
make tests/threads/priority-sema.result
make tests/threads/priority-condvar.result
```

전체 threads 테스트는 다음 명령으로 실행한다.

```bash
make check
```
