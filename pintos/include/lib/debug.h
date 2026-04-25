#ifndef __LIB_DEBUG_H
#define __LIB_DEBUG_H

/* GCC는 함수, 함수 인자 등에 "attribute"를 붙여
 * 그 속성을 표시할 수 있게 해 준다.
 * 자세한 내용은 GCC 매뉴얼을 참고하라. */
#define UNUSED __attribute__ ((unused))
#define NO_RETURN __attribute__ ((noreturn))
#define NO_INLINE __attribute__ ((noinline))
#define PRINTF_FORMAT(FMT, FIRST) __attribute__ ((format (printf, FMT, FIRST)))

/* source file 이름, line 번호, 함수 이름과 사용자 지정 메시지를 출력한 뒤
 * OS를 멈춘다. */
#define PANIC(...) debug_panic (__FILE__, __LINE__, __func__, __VA_ARGS__)

void debug_panic (const char *file, int line, const char *function,
		const char *message, ...) PRINTF_FORMAT (4, 5) NO_RETURN;
void debug_backtrace (void);

#endif



/* 이것은 header guard 바깥에 있다.
 * debug.h가 NDEBUG 설정을 달리하여 여러 번 include될 수 있게 하기 위해서다. */
#undef ASSERT
#undef NOT_REACHED

#ifndef NDEBUG
#define ASSERT(CONDITION)                                       \
	if ((CONDITION)) { } else {                             \
		PANIC ("assertion `%s' failed.", #CONDITION);   \
	}
#define NOT_REACHED() PANIC ("executed an unreachable statement");
#else
#define ASSERT(CONDITION) ((void) 0)
#define NOT_REACHED() for (;;)
#endif /* lib/debug.h */
