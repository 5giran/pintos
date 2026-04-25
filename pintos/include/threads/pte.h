#ifndef THREADS_PTE_H
#define THREADS_PTE_H

#include "threads/vaddr.h"

/* x86 hardware page table을 다루는 함수와 매크로.
 * virtual address에 대한 더 일반적인 함수와 매크로는 vaddr.h를 참고하라.
 *
 * virtual address는 다음과 같이 구성된다:
 *  63          48 47            39 38            30 29            21 20         12 11         0
 * +-------------+----------------+----------------+----------------+-------------+------------+
 * | Sign Extend |    Page-Map    | Page-Directory | Page-directory |  Page-Table |  Physical  |
 * |             | Level-4 Offset |    Pointer     |     Offset     |   Offset    |   Offset   |
 * +-------------+----------------+----------------+----------------+-------------+------------+
 *               |                |                |                |             |            |
 *               +------- 9 ------+------- 9 ------+------- 9 ------+----- 9 -----+---- 12 ----+
 *                                         Virtual Address
 */

#define PML4SHIFT 39UL
#define PDPESHIFT 30UL
#define PDXSHIFT  21UL
#define PTXSHIFT  12UL

#define PML4(la)  ((((uint64_t) (la)) >> PML4SHIFT) & 0x1FF)
#define PDPE(la) ((((uint64_t) (la)) >> PDPESHIFT) & 0x1FF)
#define PDX(la)  ((((uint64_t) (la)) >> PDXSHIFT) & 0x1FF)
#define PTX(la)  ((((uint64_t) (la)) >> PTXSHIFT) & 0x1FF)
#define PTE_ADDR(pte) ((uint64_t) (pte) & ~0xFFF)

/* 중요한 플래그는 아래와 같다.
   PDE나 PTE가 "present"가 아니면 다른 플래그는 무시된다.
   PDE나 PTE를 0으로 초기화하면 "not present"로 해석되며,
   이는 전혀 문제 없다. */
#define PTE_FLAGS 0x00000000000000fffUL    /* 플래그 비트. */
#define PTE_ADDR_MASK  0xffffffffffffff000UL /* 주소 비트. */
#define PTE_AVL   0x00000e00             /* OS가 사용할 수 있는 비트. */
#define PTE_P 0x1                        /* 1=존재함, 0=존재하지 않음. */
#define PTE_W 0x2                        /* 1=읽기/쓰기, 0=읽기 전용. */
#define PTE_U 0x4                        /* 1=user/kernel, 0=kernel 전용. */
#define PTE_A 0x20                       /* 1=접근됨, 0=접근되지 않음. */
#define PTE_D 0x40                       /* 1=dirty, 0=not dirty(PTE 전용). */

#endif /* threads/pte.h */
