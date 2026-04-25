#ifndef __LIB_STDIO_H
#define __LIB_STDIO_H

#include <debug.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 상황에 맞게 lib/user/stdio.h 또는 lib/kernel/stdio.h를 포함한다. */
#include_next <stdio.h>

/* 미리 정의된 파일 핸들. */
#define STDIN_FILENO 0
#define STDOUT_FILENO 1

/* 표준 함수. */
int printf (const char *, ...) PRINTF_FORMAT (1, 2);
int snprintf (char *, size_t, const char *, ...) PRINTF_FORMAT (3, 4);
int vprintf (const char *, va_list) PRINTF_FORMAT (1, 0);
int vsnprintf (char *, size_t, const char *, va_list) PRINTF_FORMAT (3, 0);
int putchar (int);
int puts (const char *);

/* 비표준 함수. */
void hex_dump (uintptr_t ofs, const void *, size_t size, bool ascii);

/* 내부 함수. */
void __vprintf (const char *format, va_list args,
		void (*output) (char, void *), void *aux);
void __printf (const char *format,
		void (*output) (char, void *), void *aux, ...);

/* 도움이 되도록 한다. */
#define sprintf dont_use_sprintf_use_snprintf
#define vsprintf dont_use_vsprintf_use_vsnprintf

#endif /* lib/stdio.h */
