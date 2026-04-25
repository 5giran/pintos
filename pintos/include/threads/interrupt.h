#ifndef THREADS_INTERRUPT_H
#define THREADS_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

/* interrupt가 켜져 있는가, 꺼져 있는가? */
enum intr_level {
	INTR_OFF,             /* interrupt 비활성화. */
	INTR_ON               /* interrupt 활성화. */
};

enum intr_level intr_get_level (void);
enum intr_level intr_set_level (enum intr_level);
enum intr_level intr_enable (void);
enum intr_level intr_disable (void);

/* interrupt stack frame(인터럽트 스택 프레임). */
struct gp_registers {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t rbp;
	uint64_t rdx;
	uint64_t rcx;
	uint64_t rbx;
	uint64_t rax;
} __attribute__((packed));

struct intr_frame {
	/* intr-stubs.S의 intr_entry가 push한 값들.
	   중단된 task의 저장된 register들이다. */
	struct gp_registers R;
	uint16_t es;
	uint16_t __pad1;
	uint32_t __pad2;
	uint16_t ds;
	uint16_t __pad3;
	uint32_t __pad4;
	/* intr-stubs.S의 intrNN_stub가 push한 값. */
	uint64_t vec_no; /* interrupt vector 번호. */
/* 때로는 CPU가 push하고,
   그렇지 않은 경우에는 일관성을 위해 intrNN_stub가 0으로 push한다.
   CPU는 이것을 `eip' 바로 아래에 두지만, 우리는 여기로 옮긴다. */
	uint64_t error_code;
/* CPU가 push한 값들.
   중단된 task의 저장된 register들이다. */
	uintptr_t rip;
	uint16_t cs;
	uint16_t __pad5;
	uint32_t __pad6;
	uint64_t eflags;
	uintptr_t rsp;
	uint16_t ss;
	uint16_t __pad7;
	uint32_t __pad8;
} __attribute__((packed));

typedef void intr_handler_func (struct intr_frame *);

void intr_init (void);
void intr_register_ext (uint8_t vec, intr_handler_func *, const char *name);
void intr_register_int (uint8_t vec, int dpl, enum intr_level,
                        intr_handler_func *, const char *name);
bool intr_context (void);
void intr_yield_on_return (void);

void intr_dump_frame (const struct intr_frame *);
const char *intr_name (uint8_t vec);

#endif /* threads/interrupt.h */
