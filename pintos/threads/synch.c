/* 이 파일은 Nachos 교육용 운영체제의 소스 코드에서 파생되었다.
   Nachos 저작권 고지는 아래에 전문을 다시 실었다. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.
   이 소프트웨어와 그 문서는 어떤 목적이든, 수수료 없이 그리고
   별도의 서면 계약 없이 사용, 복제, 수정, 배포할 수 있다. 단,
   위 저작권 고지와 아래 두 문단이 이 소프트웨어의 모든 복사본에
   포함되어야 한다.
   어떠한 경우에도 UNIVERSITY OF CALIFORNIA는 이 소프트웨어와
   문서의 사용으로 인해 발생한 직접적, 간접적, 특수한, 부수적,
   결과적 손해에 대해 책임지지 않는다. 이는 UNIVERSITY OF
   CALIFORNIA가 그러한 손해 가능성을 사전에 통지받은 경우에도
   마찬가지다.
   UNIVERSITY OF CALIFORNIA는 상품성 및 특정 목적 적합성에 대한
   묵시적 보증을 포함하되 이에 한정되지 않는 어떠한 보증도 명시적으로
   부인한다. 여기서 제공되는 소프트웨어는 "AS IS" 상태로 제공되며,
   UNIVERSITY OF CALIFORNIA는 유지보수, 지원, 업데이트, 개선,
   수정 사항을 제공할 의무가 없다.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* semaphore SEMA를 VALUE로 초기화한다. semaphore는 음이 아닌 정수와,
   이를 조작하는 두 개의 atomic 연산으로 이루어진다:
   - down 또는 "P": 값이 양수가 될 때까지 기다린 뒤 감소시킨다.
   - up 또는 "V": 값을 증가시키고(필요하다면) 기다리는 thread 하나를 깨운다. */
void sema_init(struct semaphore *sema, unsigned value)
{
	ASSERT(sema != NULL);

	sema->value = value;
	list_init(&sema->waiters);
}



/* semaphore에 대한 down 또는 "P" 연산.
   SEMA의 값이 양수가 될 때까지 기다린 뒤 원자적으로 감소시킨다.
   이 함수는 sleep할 수 있으므로 interrupt handler 안에서 호출하면 안 된다.
   interrupt가 비활성화된 상태에서 호출할 수는 있지만, sleep하게 되면
   다음에 스케줄되는 thread가 interrupt를 다시 켤 가능성이 높다.
   이것이 sema_down 함수다. */
void sema_down(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);
	ASSERT(!intr_context());

	old_level = intr_disable();
	while (sema->value == 0)
	{
		list_insert_ordered(&sema->waiters, &thread_current()->elem, thread_priority_compare, NULL);
		thread_block();
	}
	sema->value--;
	intr_set_level(old_level);
}

/* semaphore 값이 이미 0이 아닐 때만 down 또는 "P" 연산을 수행한다.
   semaphore를 감소시켰으면 true, 아니면 false를 반환한다.
   이 함수는 interrupt handler에서 호출할 수 있다. */
bool sema_try_down(struct semaphore *sema)
{
	enum intr_level old_level;
	bool success;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level(old_level);

	return success;
}

/* semaphore에 대한 up 또는 "V" 연산.
   SEMA의 값을 증가시키고, 기다리는 thread가 있다면 그중 하나를 깨운다.
   이 함수는 interrupt handler에서 호출할 수 있다. */
void sema_up(struct semaphore *sema)
{
	enum intr_level old_level;
	// 깨울 스레드 포인터 초기화
	struct thread *t = NULL;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	// 깨울 스레드가 있다면 깨운다.
	if (!list_empty (&sema->waiters)) {
    	// list_sort (&sema->waiters, thread_priority_compare, NULL);
    	// 깨울 스레드 선택한다. 우선순위가 가장 높은 스레드를 선택한다.
		t = list_entry (list_pop_front (&sema->waiters),
                                struct thread, elem);
		// 깨운다.
		thread_unblock (t);
  	}
	
	// if (!list_empty(&sema->waiters)) {
	// 	struct thread *t;
	// 	t = list_entry(list_pop_front(&sema->waiters), struct thread, elem);
	// 	thread_unblock(t);
	// }
	sema->value++;
	intr_set_level(old_level);

	// 깨운 스레드가 현재 스레드보다 우선순위가 높다면 yield한다.
	if (t != NULL && t->priority > thread_current()->priority) {
		if (!intr_context()) {
			thread_yield();
		}
		else {
			intr_yield_on_return();
		}	
	}
}

static void sema_test_helper(void *sema_);

/* semaphore의 자기 테스트.
   두 thread 사이에서 제어가 "ping-pong"처럼 오가게 만든다.
   무슨 일이 일어나는지 보려면 printf() 호출을 넣어 보라. */
void sema_self_test(void)
{
	struct semaphore sema[2];
	int i;

	sema_init(&sema[0], 0);
	sema_init(&sema[1], 0);
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
}

/* sema_self_test()에서 사용하는 thread 함수. */
static void
sema_test_helper(void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
	}
}

/* LOCK을 초기화한다. lock은 어떤 시점에도 최대 하나의 thread만 가질 수 있다.
   우리의 lock은 "recursive"하지 않다. 즉, 현재 lock을 가진 thread가
   같은 lock을 다시 획득하려 하면 오류다.
   lock은 초기값이 1인 semaphore의 특수화라고 볼 수 있다. lock과 그런
   semaphore의 차이는 두 가지다. 첫째, semaphore는 값이 1보다 클 수 있지만
   lock은 한 번에 하나의 thread만 소유할 수 있다. 둘째, semaphore에는
   owner 개념이 없어서 한 thread가 "down"한 뒤 다른 thread가 "up"할 수 있지만,
   lock에서는 같은 thread가 획득과 해제를 모두 해야 한다. 이런 제약이
   거슬리기 시작한다면 lock 대신 semaphore를 써야 한다는 신호다. */
void lock_init(struct lock *lock)
{
	ASSERT(lock != NULL);

	lock->holder = NULL;
	sema_init(&lock->semaphore, 1);
}

/* 필요하다면 사용 가능해질 때까지 sleep하면서 LOCK을 획득한다.
   현재 thread가 이미 이 lock을 가지고 있어서는 안 된다.
   이 함수는 sleep할 수 있으므로 interrupt handler 안에서 호출하면 안 된다.
   interrupt가 비활성화된 상태에서도 호출할 수 있지만, sleep이 필요하면
   interrupt는 다시 활성화된다. */
void lock_acquire(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));

	sema_down(&lock->semaphore);
	lock->holder = thread_current();
}

/* LOCK 획득을 시도하고, 성공하면 true, 실패하면 false를 반환한다.
   현재 thread가 이미 이 lock을 가지고 있어서는 안 된다.
   이 함수는 sleep하지 않으므로 interrupt handler 안에서 호출할 수 있다. */
bool lock_try_acquire(struct lock *lock)
{
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock));

	success = sema_try_down(&lock->semaphore);
	if (success)
		lock->holder = thread_current();
	return success;
}

/* 현재 thread가 소유하고 있어야 하는 LOCK을 해제한다.
   이것이 lock_release 함수다.
   interrupt handler는 lock을 획득할 수 없으므로, interrupt handler 안에서
   lock을 해제하려고 시도하는 것은 의미가 없다. */
void lock_release(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));

	lock->holder = NULL;
	sema_up(&lock->semaphore);
}

/* 현재 thread가 LOCK을 가지고 있으면 true, 아니면 false를 반환한다.
   (다른 thread가 lock을 가지고 있는지 검사하는 것은 race condition이 생긴다.) */
bool lock_held_by_current_thread(const struct lock *lock)
{
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/* 리스트 안의 semaphore 하나. */
struct semaphore_elem
{
	struct list_elem elem;		/* list 원소. */
	struct semaphore semaphore; /* 이 semaphore. */
};

/* semaphore 비교 함수 */
bool semaphore_priority_compare(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{

	// list_elem을 실제 thread 구조체 포인터로 변환한다.
	const struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
	const struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);

	const struct thread *ta = list_entry (list_front (&sa->semaphore.waiters), struct thread, elem);
	const struct thread *tb = list_entry (list_front (&sb->semaphore.waiters), struct thread, elem);
	// 우선순위가 높은 스레드가 ready_list의 앞쪽에 오도록 한다.
	// 같은 우선순위에서는 false가 되므로 FIFO 순서가 유지된다.
	return ta->priority > tb->priority;
}

/* condition variable COND를 초기화한다. condition variable은
   어떤 코드가 조건을 signal하고, 협력하는 코드가 그 signal을 받아
   대응하도록 해 준다. */
void cond_init(struct condition *cond)
{
	ASSERT(cond != NULL);

	list_init(&cond->waiters);
}

/* LOCK을 원자적으로 해제하고, 다른 코드가 COND를 signal할 때까지 기다린다.
   COND가 signal되면 반환 전에 LOCK을 다시 획득한다. 이 함수를 호출하기 전에
   LOCK을 이미 가지고 있어야 한다.
   이 함수가 구현하는 monitor는 "Hoare" 스타일이 아니라 "Mesa" 스타일이다.
   즉, signal을 보내는 것과 받는 것이 원자적 연산이 아니다. 따라서 일반적으로는
   wait가 끝난 뒤 호출자가 조건을 다시 검사하고, 필요하면 다시 기다려야 한다.
   하나의 condition variable은 오직 하나의 lock과만 연결되지만,
   하나의 lock은 여러 condition variable과 연결될 수 있다.
   즉, lock에서 condition variable로는 one-to-many 매핑이다.
   이 함수는 sleep할 수 있으므로 interrupt handler 안에서 호출하면 안 된다.
   interrupt가 비활성화된 상태에서도 호출할 수 있지만, sleep이 필요하면
   interrupt는 다시 활성화된다. */
void cond_wait(struct condition *cond, struct lock *lock)
{
	struct semaphore_elem waiter;

	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	// waiter.semaphore.value = 0 으로 초기화
	sema_init(&waiter.semaphore, 0);
	// waiter.elem = lock->holder->elem;
	// list_insert_ordered(&cond->waiters, lock->holder, semaphore_priority_compare, NULL);
	// list_insert_ordered(&cond->waiters, &waiter.elem, semaphore_priority_compare, NULL);
	
	list_push_back (&cond->waiters, &waiter.elem);
	lock_release(lock);
	sema_down(&waiter.semaphore);
	lock_acquire(lock);
}

/* COND에서 기다리는 thread가 있다면(LOCK으로 보호됨),
   그중 하나를 깨우도록 signal한다. 이 함수를 호출하기 전에
   LOCK을 이미 가지고 있어야 한다.
   interrupt handler는 lock을 획득할 수 없으므로, interrupt handler 안에서
   condition variable에 signal을 보내려는 시도는 의미가 없다. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	if (!list_empty(&cond->waiters)) {
		list_sort(&cond->waiters, semaphore_priority_compare, NULL);
		sema_up(&list_entry(list_pop_front(&cond->waiters),
							struct semaphore_elem, elem)
					 ->semaphore);
	// list_pop_front(&cond->waiters);
	}
}

/* COND에서 기다리는 모든 thread를 깨운다(있다면, LOCK으로 보호됨).
   이 함수를 호출하기 전에 LOCK을 이미 가지고 있어야 한다.
   interrupt handler는 lock을 획득할 수 없으므로, interrupt handler 안에서
   condition variable에 signal을 보내려는 시도는 의미가 없다. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	while (!list_empty(&cond->waiters))
		cond_signal(cond, lock);
}
