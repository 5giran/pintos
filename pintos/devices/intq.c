#include "devices/intq.h"
#include <debug.h>
#include "threads/thread.h"

static int next (int pos);
static void wait (struct intq *q, struct thread **waiter);
static void signal (struct intq *q, struct thread **waiter);

/* 인터럽트 큐 Q를 초기화한다. */
void
intq_init (struct intq *q) {
	lock_init (&q->lock);
	q->not_full = q->not_empty = NULL;
	q->head = q->tail = 0;
}

/* Q가 비어 있으면 true를, 아니면 false를 반환한다. */
bool
intq_empty (const struct intq *q) {
	ASSERT (intr_get_level () == INTR_OFF);
	return q->head == q->tail;
}

/* Q가 가득 차 있으면 true를, 아니면 false를 반환한다. */
bool
intq_full (const struct intq *q) {
	ASSERT (intr_get_level () == INTR_OFF);
	return next (q->head) == q->tail;
}

/* Q에서 바이트 하나를 제거해 반환한다.
   인터럽트 핸들러에서 호출한다면 Q가 비어 있으면 안 된다.
   그렇지 않으면 Q가 비어 있을 때 먼저 바이트가 추가될 때까지 잠든다. */
uint8_t
intq_getc (struct intq *q) {
	uint8_t byte;

	ASSERT (intr_get_level () == INTR_OFF);
	while (intq_empty (q)) {
		ASSERT (!intr_context ());
		lock_acquire (&q->lock);
		wait (q, &q->not_empty);
		lock_release (&q->lock);
	}

	byte = q->buf[q->tail];
	q->tail = next (q->tail);
	signal (q, &q->not_full);
	return byte;
}

/* BYTE를 Q의 끝에 추가한다.
   인터럽트 핸들러에서 호출한다면 Q가 가득 차 있으면 안 된다.
   그렇지 않으면 Q가 가득 차 있을 때 먼저 바이트가 제거될 때까지 잠든다. */
void
intq_putc (struct intq *q, uint8_t byte) {
	ASSERT (intr_get_level () == INTR_OFF);
	while (intq_full (q)) {
		ASSERT (!intr_context ());
		lock_acquire (&q->lock);
		wait (q, &q->not_full);
		lock_release (&q->lock);
	}

	q->buf[q->head] = byte;
	q->head = next (q->head);
	signal (q, &q->not_empty);
}

/* intq에서 POS 다음 위치를 반환한다. */
static int
next (int pos) {
	return (pos + 1) % INTQ_BUFSIZE;
}

/* WAITER는 Q의 not_empty 또는 not_full 멤버의 주소여야 한다.
   주어진 조건이 참이 될 때까지 기다린다. */
static void
wait (struct intq *q UNUSED, struct thread **waiter) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT ((waiter == &q->not_empty && intq_empty (q))
			|| (waiter == &q->not_full && intq_full (q)));

	*waiter = thread_current ();
	thread_block ();
}

/* WAITER는 Q의 not_empty 또는 not_full 멤버의 주소여야 하며, 연관된 조건이 참이어야 한다.
   조건을 기다리는 스레드가 있으면 깨우고 대기 중인 스레드를 리셋한다. */
static void
signal (struct intq *q UNUSED, struct thread **waiter) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT ((waiter == &q->not_empty && !intq_empty (q))
			|| (waiter == &q->not_full && !intq_full (q)));

	if (*waiter != NULL) {
		thread_unblock (*waiter);
		*waiter = NULL;
	}
}
