#ifndef THREADS_INIT_H
#define THREADS_INIT_H

#include <debug.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* physical memory 크기(4 kB page 단위). */
extern size_t ram_pages;

/* kernel 매핑만 가진 Page map level 4. */
extern uint64_t *base_pml4;

/* -q: kernel 작업이 끝나면 전원을 끌 것인가? */
extern bool power_off_when_done;

void power_off (void) NO_RETURN;

#endif /* threads/init.h */
