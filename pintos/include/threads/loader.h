#ifndef THREADS_LOADER_H
#define THREADS_LOADER_H

/* PC BIOS가 고정한 상수들. */
#define LOADER_BASE 0x7c00      /* loader 시작의 physical address. */
#define LOADER_END  0x7e00      /* loader 끝의 physical address. */

/* kernel base의 physical address. */
#define LOADER_KERN_BASE 0x8004000000

/* 모든 physical memory가 매핑되는 kernel virtual address. */
#define LOADER_PHYS_BASE 0x200000

/* Multiboot 정보 */
#define MULTIBOOT_INFO       0x7000
#define MULTIBOOT_FLAG       MULTIBOOT_INFO
#define MULTIBOOT_MMAP_LEN   MULTIBOOT_INFO + 44
#define MULTIBOOT_MMAP_ADDR  MULTIBOOT_INFO + 48

#define E820_MAP MULTIBOOT_INFO + 52
#define E820_MAP4 MULTIBOOT_INFO + 56

/* 중요한 loader physical address들. */
#define LOADER_SIG (LOADER_END - LOADER_SIG_LEN)   /* 0xaa55 BIOS 시그니처 */
#define LOADER_ARGS (LOADER_SIG - LOADER_ARGS_LEN)     /* command-line 인자. */
#define LOADER_ARG_CNT (LOADER_ARGS - LOADER_ARG_CNT_LEN) /* 인자 개수. */

/* loader data structure의 크기들. */
#define LOADER_SIG_LEN 2
#define LOADER_ARGS_LEN 128
#define LOADER_ARG_CNT_LEN 4

/* loader가 정의한 GDT selector.
   더 많은 selector는 userprog/gdt.h에 정의되어 있다. */
#define SEL_NULL        0x00    /* null selector(널 셀렉터) */
#define SEL_KCSEG       0x08    /* kernel code selector(커널 코드 셀렉터) */
#define SEL_KDSEG       0x10    /* kernel data selector(커널 데이터 셀렉터) */
#define SEL_UDSEG       0x1B    /* user data selector(유저 데이터 셀렉터) */
#define SEL_UCSEG       0x23    /* user code selector(유저 코드 셀렉터) */
#define SEL_TSS         0x28    /* task-state segment(태스크 상태 세그먼트) */
#define SEL_CNT         8       /* segment 개수. */

#endif /* threads/loader.h */
