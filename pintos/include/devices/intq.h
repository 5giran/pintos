#ifndef DEVICES_INTQ_H
#define DEVICES_INTQ_H

#include "threads/interrupt.h"
#include "threads/synch.h"

/* "interrupt queue", 즉 kernel thread와 external interrupt handler가
   공유하는 원형 buffer.
   interrupt queue 함수는 kernel thread나 external interrupt handler에서
   호출할 수 있다. intq_init()을 제외하면 어느 경우든 interrupt가 꺼져 있어야 한다.
   interrupt queue는 "monitor" 구조를 가진다. 이 경우 threads/synch.h의
   lock과 condition variable은 평소처럼 사용할 수 없다.
   그것들은 kernel thread끼리만 보호할 수 있고 interrupt handler로부터는
   보호하지 못하기 때문이다. */

/* queue buffer 크기(바이트 단위). */
#define INTQ_BUFSIZE 64

/* byte 단위 원형 queue. */
struct intq {
	/* 기다리는 thread들. */
	struct lock lock;           /* 한 번에 하나의 thread만 기다릴 수 있다. */
	struct thread *not_full;    /* not-full 조건을 기다리는 thread. */
	struct thread *not_empty;   /* not-empty 조건을 기다리는 thread. */

	/* 큐. */
	uint8_t buf[INTQ_BUFSIZE];  /* 버퍼. */
	int head;                   /* 새 데이터는 여기에 기록된다. */
	int tail;                   /* 오래된 데이터는 여기서 읽힌다. */
};

void intq_init (struct intq *);
bool intq_empty (const struct intq *);
bool intq_full (const struct intq *);
uint8_t intq_getc (struct intq *);
void intq_putc (struct intq *, uint8_t);

#endif /* devices/intq.h */
