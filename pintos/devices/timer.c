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

/* 8254 Programmable Interval Timer (PIT)을 설정하여
   초당 PIT_FREQ번 인터럽트를 발생시키고, 해당 인터럽트를
   등록합니다. */
void
timer_init (void) {
	/* 8254 입력 주파수를 TIMER_FREQ로 나눈 값, 가장 가까운 정수로
	   반올림. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: counter 0, LSB 다음에 MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
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

/* 대략 TICKS timer tick 동안 실행을 중단합니다. */
void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks (); // 현재 타이머 tick값 저장: 기준 시간

	// 인터럽트가 켜져있는지 확인 - sleep은 인터럽트가 켜진 상태에만 사용가능하므로
	ASSERT (intr_get_level () == INTR_ON);
	// 잠든 시간 이후로 몇 틱 지났는지 계산 - 주어진 틱만큼 지나지 않았다면
	while (timer_elapsed (start) < ticks)
		thread_yield (); // CPU를 다른 스레드한테 양보
	// 이게 busy waiting 구조임
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

/* 타이머 인터럽트 핸들러. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
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
