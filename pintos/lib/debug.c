#include <debug.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* 호출 스택을 출력한다. 즉, 우리가 중첩되어 있는 각 함수마다 하나씩 들어 있는 주소 목록이다. gdb 또는 addr2line을
   kernel.o에 적용하면 이를 파일 이름, 줄 번호, 함수 이름으로 변환할 수 있다. */
void
debug_backtrace (void) {
	static bool explained;
	void **frame;

	printf ("Call stack:");
	for (frame = __builtin_frame_address (0);
			frame != NULL && frame[0] != NULL;
			frame = frame[0])
		printf (" %p", frame[1]);
	printf (".\n");

	if (!explained) {
		explained = true;
		printf ("The `backtrace' program can make call stacks useful.\n"
				"Read \"Backtraces\" in the \"Debugging Tools\" chapter\n"
				"of the Pintos documentation for more information.\n");
	}
}
