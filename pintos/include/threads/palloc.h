#ifndef THREADS_PALLOC_H
#define THREADS_PALLOC_H

#include <stdint.h>
#include <stddef.h>

/* page 할당 방법. */
enum palloc_flags {
	PAL_ASSERT = 001,           /* 실패 시 panic. */
	PAL_ZERO = 002,             /* page 내용을 0으로 채운다. */
	PAL_USER = 004              /* user page(유저 페이지). */
};

/* user pool에 넣을 최대 page 수. */
extern size_t user_page_limit;

uint64_t palloc_init (void);
void *palloc_get_page (enum palloc_flags);
void *palloc_get_multiple (enum palloc_flags, size_t page_cnt);
void palloc_free_page (void *);
void palloc_free_multiple (void *, size_t page_cnt);

#endif /* threads/palloc.h */
