#ifndef THREADS_VADDR_H
#define THREADS_VADDR_H

#include <debug.h>
#include <stdint.h>
#include <stdbool.h>

#include "threads/loader.h"

/* virtual address를 다루는 함수와 매크로.
 *
 * x86 hardware page table 전용 함수와 매크로는 pte.h를 참고하라. */

#define BITMASK(SHIFT, CNT) (((1ul << (CNT)) - 1) << (SHIFT))

/* page offset(비트 0:12). */
#define PGSHIFT 0                          /* 첫 번째 offset bit의 index. */
#define PGBITS  12                         /* offset bit 개수. */
#define PGSIZE  (1 << PGBITS)              /* 한 page의 바이트 수. */
#define PGMASK  BITMASK(PGSHIFT, PGBITS)   /* page offset 비트(0:12) */

/* page 내부 offset. */
#define pg_ofs(va) ((uint64_t) (va) & PGMASK)

#define pg_no(va) ((uint64_t) (va) >> PGBITS)

/* 가장 가까운 page 경계로 올림한다. */
#define pg_round_up(va) ((void *) (((uint64_t) (va) + PGSIZE - 1) & ~PGMASK))

/* 가장 가까운 page 경계로 내림한다. */
#define pg_round_down(va) (void *) ((uint64_t) (va) & ~PGMASK)

/* kernel virtual address 시작점 */
#define KERN_BASE LOADER_KERN_BASE

/* user stack 시작점 */
#define USER_STACK 0x47480000

/* VADDR가 user virtual address면 true를 반환한다. */
#define is_user_vaddr(vaddr) (!is_kernel_vaddr((vaddr)))

/* VADDR가 kernel virtual address면 true를 반환한다. */
#define is_kernel_vaddr(vaddr) ((uint64_t)(vaddr) >= KERN_BASE)

// FIXME: 검사 추가
/* physical address PADDR가 매핑되는 kernel virtual address를 반환한다. */
#define ptov(paddr) ((void *) (((uint64_t) paddr) + KERN_BASE))

/* kernel virtual address VADDR가 매핑되는 physical address를 반환한다. */
#define vtop(vaddr) \
({ \
	ASSERT(is_kernel_vaddr(vaddr)); \
	((uint64_t) (vaddr) - (uint64_t) KERN_BASE);\
})

#endif /* threads/vaddr.h */
