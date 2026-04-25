#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* 8254 타이머 칩의 하드웨어 세부 사항은 [8254]를 참조하세요. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* OS가 부팅된 이후의 타이머 tick 수. */
static int64_t ticks;

/* timer tick당 loops 수.
   timer_calibrate()에 의해 초기화됩니다. */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* Blocked 스레드들이 대기하고 있는 공간 */
static struct list sleep_list;

/* list_insert_ordered() 함수의 세 번째 인자로 넘겨주어야 하는 weakup_ticks 비교 기준 함수 
	 */
static bool wakeup_ticks_less(struct list_elem *a, struct list_elem *b, void *aux UNUSED);

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
void
timer_init (void) {
	/* 8254 입력 주파수를 TIMER_FREQ로 나눈 값, 가장 가까운 정수로
	   반올림. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: counter 0, LSB 다음에 MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
	/* sleep_list 초기화 */
	list_init(&sleep_list);
}

/* 짧은 지연을 구현하는 데 쓰이는 loops_per_tick를 보정합니다. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* loops_per_tick를 아직 한 timer tick보다 작은
	   가장 큰 2의 거듭제곱으로 근사합니다. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* loops_per_tick의 다음 8비트를 세밀하게 조정합니다. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* OS 부팅 이후의 timer tick 수를 반환합니다. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* THEN 이후 경과한 timer tick 수를 반환합니다. 이는
   이전에 timer_ticks()가 반환한 값이어야 합니다. */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}
/* 스레드 a의 wakeup_ticks가 스레드 b의 wakeup_ticks보다 빠를 경우 true 반환, 같거나 느리면 false 반환.
	 첫 번째, 두 번째 인자로 스레드 구조체 멤버인 elem을 받기 떄문에, 함수 내에서 thread로 변환하는 과정 필요. */
static bool wakeup_ticks_less(struct list_elem *a, struct list_elem *b, void *aux UNUSED) {
	struct thread *thread_a = list_entry(a, struct thread, elem);
	struct thread *thread_b = list_entry(b, struct thread, elem);

	return (thread_a->wakeup_ticks < thread_b->wakeup_ticks);
}

/* Suspends execution for approximately TICKS timer ticks. */
void
timer_sleep (int64_t ticks) {
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
}

/* 대략 MS 밀리초 동안 실행을 중단합니다. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* 대략 US 마이크로초 동안 실행을 중단합니다. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* 대략 NS 나노초 동안 실행을 중단합니다. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* 타이머 통계를 출력합니다. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

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

/* LOOPS 반복이 하나 이상의 timer tick을 기다리면 true를 반환하고,
   그렇지 않으면 false를 반환합니다. */
static bool
too_many_loops (unsigned loops) {
	/* timer tick을 기다립니다. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* LOOPS번 루프를 실행합니다. */
	start = ticks;
	busy_wait (loops);

	/* tick 수가 바뀌었다면 너무 오래 반복한 것입니다. */
	barrier ();
	return start != ticks;
}

/* 짧은 지연을 구현하기 위해 단순 루프를 LOOPS번
   반복합니다.
   코드 정렬이 타이밍에 큰 영향을 줄 수 있으므로 NO_INLINE으로
   지정했습니다. 따라서 이 함수가 위치마다 다르게 인라인되면
   결과를 예측하기 어려워집니다. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* 대략 NUM/DENOM초 동안 잠듭니다. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* NUM/DENOM초를 timer tick으로 변환하되, 내림합니다.
	 *
	 * (NUM / DENOM) s
	 * ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	 * 1 s / TIMER_FREQ ticks */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* 적어도 하나의 완전한 timer tick을 기다리고 있습니다.  CPU를 다른 프로세스에 양보하므로
		   timer_sleep()을 사용하세요. */
		timer_sleep (ticks);
	} else {
		/* 그렇지 않으면 더 정확한
		   sub-tick 타이밍을 위해 busy-wait 루프를 사용합니다. 분자와 분모를
		   1000으로 줄여 오버플로 가능성을 피합니다. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
