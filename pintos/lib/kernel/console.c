#include <console.h>
#include <stdarg.h>
#include <stdio.h>
#include "devices/serial.h"
#include "devices/vga.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/synch.h"

static void vprintf_helper (char, void *);
static void putchar_have_lock (uint8_t c);

/* 콘솔 락.
   vga와 serial 계층은 각각 자체적인 락을 사용하므로, 언제든지 호출해도 안전합니다.
   하지만 이 락은 동시에 호출된 printf()가 출력을 섞어 혼란스럽게 만드는 것을 막는 데 유용합니다. */
static struct lock console_lock;

/* 일반적인 상황에서는 true입니다. 위에서 설명했듯이 스레드 간 출력이 섞이지 않도록 콘솔 락을 사용하고 싶기 때문입니다.

   락이 아직 동작하지 않거나 콘솔 락이 초기화되기 전인 초기 부팅 단계, 또는 커널 패닉 이후에는 false입니다. 첫 번째 경우에는
   락을 잡으면 assertion failure가 발생하고, 그 결과 패닉이 나서 결국 두 번째 경우가 됩니다. 두 번째 경우에는, 패닉을
   유발한 것이 버그가 있는 lock_acquire() 구현이라면, 아마 그냥 재귀적으로 다시 들어가게 될 것입니다. */
static bool use_console_lock;

/* Pintos에 충분한 디버그 출력을 추가하면, 단일 스레드에서 console_lock을 재귀적으로 잡아 보려 할 수 있습니다. 실제
   예로, palloc_free()에 printf() 호출을 하나 추가했습니다.
   그 결과 나온 실제 backtrace는 다음과 같습니다:

   lock_console()
   vprintf()
   printf()             - palloc()가 다시 락을 잡으려 함
   palloc_free()
   schedule_tail()      - 스레드 전환 중 다른 스레드가 죽음
   schedule()
   thread_yield()
   intr_handler()       - 타이머 인터럽트
   intr_set_level()
   serial_putc()
   putchar_have_lock()
   putbuf()
   sys_write()          - 한 프로세스가 콘솔에 쓰는 중
   syscall_handler()
   intr_handler()

   이런 종류의 문제는 디버깅하기 매우 어렵기 때문에, 우리는 깊이 카운터를 사용해 재귀 락을 흉내 내는 방식으로 이 문제를 피합니다. */
static int console_lock_depth;

/* 콘솔에 기록된 문자 수. */
static int64_t write_cnt;

/* 콘솔 락을 활성화합니다. */
void
console_init (void) {
	lock_init (&console_lock);
	use_console_lock = true;
}

/* 커널 패닉이 진행 중임을 콘솔에 알리며, 이제부터는 콘솔 락을 잡으려 시도하지 않도록 경고합니다. */
void
console_panic (void) {
	use_console_lock = false;
}

/* 콘솔 통계를 출력합니다. */
void
console_print_stats (void) {
	printf ("Console: %lld characters output\n", write_cnt);
}

/* 콘솔 락을 획득합니다. */
	static void
acquire_console (void) {
	if (!intr_context () && use_console_lock) {
		if (lock_held_by_current_thread (&console_lock)) 
			console_lock_depth++; 
		else
			lock_acquire (&console_lock); 
	}
}

/* 콘솔 락을 해제합니다. */
static void
release_console (void) {
	if (!intr_context () && use_console_lock) {
		if (console_lock_depth > 0)
			console_lock_depth--;
		else
			lock_release (&console_lock); 
	}
}

/* 현재 스레드가 콘솔 락을 가지고 있으면 true를, 그렇지 않으면 false를 반환합니다. */
static bool
console_locked_by_current_thread (void) {
	return (intr_context ()
			|| !use_console_lock
			|| lock_held_by_current_thread (&console_lock));
}

/* 표준 vprintf() 함수로,
   printf()와 비슷하지만 va_list를 사용합니다.
   출력을 vga 디스플레이와 serial 포트 양쪽에 씁니다. */
int
vprintf (const char *format, va_list args) {
	int char_cnt = 0;

	acquire_console ();
	__vprintf (format, args, vprintf_helper, &char_cnt);
	release_console ();

	return char_cnt;
}

/* 문자열 S를 콘솔에 쓰고, 뒤이어 개행
   문자를 붙입니다. */
int
puts (const char *s) {
	acquire_console ();
	while (*s != '\0')
		putchar_have_lock (*s++);
	putchar_have_lock ('\n');
	release_console ();

	return 0;
}

/* BUFFER의 N개 문자를 콘솔에 씁니다. */
void
putbuf (const char *buffer, size_t n) {
	acquire_console ();
	while (n-- > 0)
		putchar_have_lock (*buffer++);
	release_console ();
}

/* C를 vga 디스플레이와 serial 포트에 씁니다. */
int
putchar (int c) {
	acquire_console ();
	putchar_have_lock (c);
	release_console ();

	return c;
}

/* vprintf()를 위한 도우미 함수. */
static void
vprintf_helper (char c, void *char_cnt_) {
	int *char_cnt = char_cnt_;
	(*char_cnt)++;
	putchar_have_lock (c);
}

/* C를 vga 디스플레이와 serial 포트에 씁니다.
   적절하다면 호출자는 이미 콘솔 락을 획득한 상태입니다. */
static void
putchar_have_lock (uint8_t c) {
	ASSERT (console_locked_by_current_thread ());
	write_cnt++;
	serial_putc (c);
	vga_putc (c);
}
