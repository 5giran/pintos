#include "threads/interrupt.h"
#include <debug.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include "threads/flags.h"
#include "threads/intr-stubs.h"
#include "threads/io.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/gdt.h"
#endif

/* x86_64 interrupt 개수. */
#define INTR_CNT 256

/* FUNCTION을 호출하는 gate를 만든다.
   이 gate는 descriptor privilege level(DPL)을 가진다. 즉, 프로세서가
   DPL 또는 그보다 번호가 낮은 ring에 있을 때 의도적으로 호출할 수 있다.
   실제로는 DPL==3이면 user mode에서 gate 호출이 가능하고, DPL==0이면
   그런 호출을 막는다. 다만 user mode에서 발생한 fault와 exception은
   여전히 DPL==0인 gate를 호출하게 만든다.
   TYPE은 14(interrupt gate) 또는 15(trap gate)여야 한다.
   차이는 interrupt gate에 진입하면 interrupt가 비활성화되지만,
   trap gate에 진입하면 그렇지 않다는 점이다. 자세한 내용은
   [IA32-v3a] 5.12.1.2 "Flag Usage By Exception- or
   Interrupt-Handler Procedure"를 참고하라. */

struct gate {
	unsigned off_15_0 : 16;   // 세그먼트 내 offset의 하위 16비트
	unsigned ss : 16;         // 세그먼트 selector
	unsigned ist : 3;        // 인자 수, interrupt/trap gate는 0
	unsigned rsv1 : 5;        // reserved(아마 0이어야 함)
	unsigned type : 4;        // 타입(STS_{TG,IG32,TG32})
	unsigned s : 1;           // 반드시 0이어야 함(system)
	unsigned dpl : 2;         // descriptor privilege level(디스크립터 권한 레벨)
	unsigned p : 1;           // 존재 여부
	unsigned off_31_16 : 16;  // 세그먼트 내 offset의 상위 비트
	uint32_t off_32_63;
	uint32_t rsv2;
};

/* Interrupt Descriptor Table(IDT, 인터럽트 디스크립터 테이블).
   형식은 CPU가 고정한다. 자세한 내용은
   [IA32-v3a] 5.10 "Interrupt Descriptor Table (IDT)",
   5.11 "IDT Descriptors",
   5.12.1.2 "Flag Usage By Exception- or Interrupt-Handler Procedure"
   를 참고하라. */
static struct gate idt[INTR_CNT];

static struct desc_ptr idt_desc = {
	.size = sizeof(idt) - 1,
	.address = (uint64_t) idt
};


#define make_gate(g, function, d, t) \
{ \
	ASSERT ((function) != NULL); \
	ASSERT ((d) >= 0 && (d) <= 3); \
	ASSERT ((t) >= 0 && (t) <= 15); \
	*(g) = (struct gate) { \
		.off_15_0 = (uint64_t) (function) & 0xffff, \
		.ss = SEL_KCSEG, \
		.ist = 0, \
		.rsv1 = 0, \
		.type = (t), \
		.s = 0, \
		.dpl = (d), \
		.p = 1, \
		.off_31_16 = ((uint64_t) (function) >> 16) & 0xffff, \
		.off_32_63 = ((uint64_t) (function) >> 32) & 0xffffffff, \
		.rsv2 = 0, \
	}; \
}

/* 주어진 DPL로 FUNCTION을 호출하는 interrupt gate를 만든다. */
#define make_intr_gate(g, function, dpl) make_gate((g), (function), (dpl), 14)

/* 주어진 DPL로 FUNCTION을 호출하는 trap gate를 만든다. */
#define make_trap_gate(g, function, dpl) make_gate((g), (function), (dpl), 15)



/* 각 interrupt에 대한 interrupt handler 함수들. */
static intr_handler_func *intr_handlers[INTR_CNT];

/* 디버깅용 각 interrupt의 이름. */
static const char *intr_names[INTR_CNT];

/* External interrupt는 timer처럼 CPU 밖의 장치가 생성하는 interrupt다.
   External interrupt는 interrupt가 꺼진 상태에서 실행되므로 중첩되지도 않고
   선점되지도 않는다. external interrupt의 handler는 sleep할 수도 없지만,
   interrupt가 반환되기 직전에 새 프로세스를 스케줄해 달라고
   intr_yield_on_return()을 호출할 수는 있다. */
static bool in_external_intr;   /* 현재 external interrupt를 처리 중인가? */
static bool yield_on_return;    /* interrupt 반환 시 yield해야 하는가? */

/* Programmable Interrupt Controller helper 함수들. */
static void pic_init (void);
static void pic_end_of_interrupt (int irq);

/* interrupt handler들. */
void intr_handler (struct intr_frame *args);

/* 현재 interrupt 상태를 반환한다. */
enum intr_level
intr_get_level (void) {
	uint64_t flags;

	/* flags register를 프로세서 stack에 push한 뒤, stack에서 값을
	   `flags'로 pop한다. [IA32-v2b] "PUSHF", "POP"과
	   [IA32-v3a] 5.8.1 "Masking Maskable Hardware Interrupts"를
	   참고하라. */
	asm volatile ("pushfq; popq %0" : "=g" (flags));

	return flags & FLAG_IF ? INTR_ON : INTR_OFF;
}

/* LEVEL에 따라 interrupt를 활성화 또는 비활성화하고,
   이전 interrupt 상태를 반환한다. */
enum intr_level
intr_set_level (enum intr_level level) {
	return level == INTR_ON ? intr_enable () : intr_disable ();
}

/* interrupt를 활성화하고 이전 interrupt 상태를 반환한다. */
enum intr_level
intr_enable (void) {
	enum intr_level old_level = intr_get_level ();
	ASSERT (!intr_context ());

	/* interrupt flag를 세팅하여 interrupt를 활성화한다.
	   [IA32-v2b] "STI"와
	   [IA32-v3a] 5.8.1 "Masking Maskable Hardware Interrupts"를 참고하라. */
	asm volatile ("sti");

	return old_level;
}

/* interrupt를 비활성화하고 이전 interrupt 상태를 반환한다. */
enum intr_level
intr_disable (void) {
	enum intr_level old_level = intr_get_level ();

	/* interrupt flag를 클리어하여 interrupt를 비활성화한다.
	   [IA32-v2b] "CLI"와
	   [IA32-v3a] 5.8.1 "Masking Maskable Hardware Interrupts"를 참고하라. */
	asm volatile ("cli" : : : "memory");

	return old_level;
}

/* interrupt 시스템을 초기화한다. */
void
intr_init (void) {
	int i;

	/* interrupt controller를 초기화한다. */
	pic_init ();

	/* IDT를 초기화한다. */
	for (i = 0; i < INTR_CNT; i++) {
		make_intr_gate(&idt[i], intr_stubs[i], 0);
		intr_names[i] = "unknown";
	}

#ifdef USERPROG
	/* TSS를 적재한다. */
	ltr (SEL_TSS);
#endif

	/* IDT register를 적재한다. */
	lidt(&idt_desc);

	/* intr_names를 초기화한다. */
	intr_names[0] = "#DE Divide Error";
	intr_names[1] = "#DB Debug Exception";
	intr_names[2] = "NMI Interrupt";
	intr_names[3] = "#BP Breakpoint Exception";
	intr_names[4] = "#OF Overflow Exception";
	intr_names[5] = "#BR BOUND Range Exceeded Exception";
	intr_names[6] = "#UD Invalid Opcode Exception";
	intr_names[7] = "#NM Device Not Available Exception";
	intr_names[8] = "#DF Double Fault Exception";
	intr_names[9] = "Coprocessor Segment Overrun";
	intr_names[10] = "#TS Invalid TSS Exception";
	intr_names[11] = "#NP Segment Not Present";
	intr_names[12] = "#SS Stack Fault Exception";
	intr_names[13] = "#GP General Protection Exception";
	intr_names[14] = "#PF Page-Fault Exception";
	intr_names[16] = "#MF x87 FPU Floating-Point Error";
	intr_names[17] = "#AC Alignment Check Exception";
	intr_names[18] = "#MC Machine-Check Exception";
	intr_names[19] = "#XF SIMD Floating-Point Exception";
}

/* interrupt VEC_NO가 descriptor privilege level DPL로 HANDLER를
   호출하도록 등록한다. 디버깅용으로 이 interrupt에 NAME을 붙인다.
   interrupt handler는 interrupt 상태가 LEVEL로 설정된 채 호출된다. */
static void
register_handler (uint8_t vec_no, int dpl, enum intr_level level,
		intr_handler_func *handler, const char *name) {
	ASSERT (intr_handlers[vec_no] == NULL);
	if (level == INTR_ON) {
		make_trap_gate(&idt[vec_no], intr_stubs[vec_no], dpl);
	}
	else {
		make_intr_gate(&idt[vec_no], intr_stubs[vec_no], dpl);
	}
	intr_handlers[vec_no] = handler;
	intr_names[vec_no] = name;
}

/* external interrupt VEC_NO가 HANDLER를 호출하도록 등록한다.
   디버깅용 이름은 NAME이다. handler는 interrupt가 비활성화된 상태로 실행된다. */
void
intr_register_ext (uint8_t vec_no, intr_handler_func *handler,
		const char *name) {
	ASSERT (vec_no >= 0x20 && vec_no <= 0x2f);
	register_handler (vec_no, 0, INTR_OFF, handler, name);
}

/* internal interrupt VEC_NO가 HANDLER를 호출하도록 등록한다.
   디버깅용 이름은 NAME이다. interrupt handler는 interrupt 상태 LEVEL로 호출된다.
   handler는 descriptor privilege level DPL을 가진다. 즉, 프로세서가
   DPL 또는 그보다 낮은 번호의 ring에 있을 때 의도적으로 호출할 수 있다.
   실제로는 DPL==3이면 user mode에서 interrupt를 호출할 수 있고,
   DPL==0이면 그런 호출이 막힌다. 하지만 user mode에서 발생한 fault와
   exception은 여전히 DPL==0 interrupt를 호출한다. 자세한 내용은
   [IA32-v3a] 4.5 "Privilege Levels"와
   4.8.1.1 "Accessing Nonconforming Code Segments"를 참고하라. */
void
intr_register_int (uint8_t vec_no, int dpl, enum intr_level level,
		intr_handler_func *handler, const char *name)
{
	ASSERT (vec_no < 0x20 || vec_no > 0x2f);
	register_handler (vec_no, dpl, level, handler, name);
}

/* external interrupt를 처리하는 동안에는 true를,
   그 외에는 false를 반환한다. */
bool
intr_context (void) {
	return in_external_intr;
}

/* external interrupt 처리 중에는 interrupt handler가
   interrupt에서 반환되기 직전에 새 프로세스에 yield하도록 지시한다.
   다른 때에는 호출하면 안 된다. */
void
intr_yield_on_return (void) {
	ASSERT (intr_context ());
	yield_on_return = true;
}

/* 8259A Programmable Interrupt Controller(프로그래머블 인터럽트 컨트롤러). */

/* 모든 PC에는 두 개의 8259A Programmable Interrupt Controller(PIC) 칩이 있다.
   하나는 포트 0x20과 0x21로 접근하는 "master"이고,
   다른 하나는 master의 IRQ 2 라인에 cascade된 "slave"이며
   포트 0xa0과 0xa1로 접근한다. 포트 0x20 접근은 A0 라인을 0으로,
   0x21 접근은 A1 라인을 1로 설정한다. slave PIC도 비슷하다.
   기본적으로 PIC가 전달하는 interrupt 0...15는 interrupt vector 0...15로 간다.
   문제는 이 vector들이 CPU trap과 exception에도 사용된다는 점이다.
   그래서 우리는 PIC를 재프로그램하여 interrupt 0...15가 대신
   interrupt vector 32...47(0x20...0x2f)로 전달되게 한다. */

/* PIC를 초기화한다. 자세한 내용은 [8259A]를 참고하라. */
static void
pic_init (void) {
	/* 두 PIC의 모든 interrupt를 mask한다. */
	outb (0x21, 0xff);
	outb (0xa1, 0xff);

	/* master를 초기화한다. */
	outb (0x20, 0x11); /* ICW1: single mode, edge-triggered, ICW4를 기대함. */
	outb (0x21, 0x20); /* ICW2: line IR0...7을 irq 0x20...0x27로 매핑 */
	outb (0x21, 0x04); /* ICW3: line IR2에 slave PIC가 연결됨. */
	outb (0x21, 0x01); /* ICW4: 8086 mode, normal EOI, 버퍼링 없음 */

	/* slave를 초기화한다. */
	outb (0xa0, 0x11); /* ICW1: single mode, edge-triggered, ICW4를 기대함. */
	outb (0xa1, 0x28); /* ICW2: line IR0...7을 irq 0x28...0x2f로 매핑 */
	outb (0xa1, 0x02); /* ICW3: slave ID는 2. */
	outb (0xa1, 0x01); /* ICW4: 8086 mode, normal EOI, 버퍼링 없음 */

	/* 모든 interrupt의 mask를 해제한다. */
	outb (0x21, 0x00);
	outb (0xa1, 0x00);
}

/* 주어진 IRQ에 대해 PIC에 end-of-interrupt 신호를 보낸다.
   IRQ를 acknowledge하지 않으면 다시는 전달되지 않으므로 중요하다. */
static void
pic_end_of_interrupt (int irq) {
	ASSERT (irq >= 0x20 && irq < 0x30);

	/* master PIC를 acknowledge한다. */
	outb (0x20, 0x20);

	/* 이것이 slave interrupt라면 slave PIC도 acknowledge한다. */
	if (irq >= 0x28)
		outb (0xa0, 0x20);
}
/* interrupt handler들. */

/* 모든 interrupt, fault, exception을 처리하는 handler.
   이 함수는 intr-stubs.S의 assembly language interrupt stub에서 호출된다.
   FRAME은 interrupt와 중단된 thread의 register를 설명한다. */
void
intr_handler (struct intr_frame *frame) {
	bool external;
	intr_handler_func *handler;

	/* External interrupt는 특별하다.
	   한 번에 하나씩만 처리하므로(즉, interrupt는 꺼져 있어야 하며)
	   PIC에 acknowledge도 해줘야 한다(아래 참고).
	   external interrupt handler는 sleep할 수 없다. */
	external = frame->vec_no >= 0x20 && frame->vec_no < 0x30;
	if (external) {
		ASSERT (intr_get_level () == INTR_OFF);
		ASSERT (!intr_context ());

		in_external_intr = true;
		yield_on_return = false;
	}

	/* interrupt의 handler를 호출한다. */
	handler = intr_handlers[frame->vec_no];
	if (handler != NULL)
		handler (frame);
	else if (frame->vec_no == 0x27 || frame->vec_no == 0x2f) {
		/* handler는 없지만, 이 interrupt는 hardware fault나
		   hardware race condition 때문에 spuriously 발생할 수 있다.
		   무시한다. */
	} else {
		/* handler도 없고 spurious도 아니다.
		   unexpected interrupt handler를 호출한다. */
		intr_dump_frame (frame);
		PANIC ("Unexpected interrupt");
	}

	/* external interrupt 처리를 마무리한다. */
	if (external) {
		ASSERT (intr_get_level () == INTR_OFF);
		ASSERT (intr_context ());

		in_external_intr = false;
		pic_end_of_interrupt (frame->vec_no);

		if (yield_on_return)
			thread_yield ();
	}
}

/* 디버깅을 위해 interrupt frame F를 console에 덤프한다. */
void
intr_dump_frame (const struct intr_frame *f) {
	/* CR2는 마지막 page fault의 linear address다.
	   [IA32-v2a] "MOV--Move to/from Control Registers"와
	   [IA32-v3a] 5.14 "Interrupt 14--Page Fault Exception (#PF)"를
	   참고하라. */
	uint64_t cr2 = rcr2();
	printf ("Interrupt %#04llx (%s) at rip=%llx\n",
			f->vec_no, intr_names[f->vec_no], f->rip);
	printf (" cr2=%016llx error=%16llx\n", cr2, f->error_code);
	printf ("rax %016llx rbx %016llx rcx %016llx rdx %016llx\n",
			f->R.rax, f->R.rbx, f->R.rcx, f->R.rdx);
	printf ("rsp %016llx rbp %016llx rsi %016llx rdi %016llx\n",
			f->rsp, f->R.rbp, f->R.rsi, f->R.rdi);
	printf ("rip %016llx r8 %016llx  r9 %016llx r10 %016llx\n",
			f->rip, f->R.r8, f->R.r9, f->R.r10);
	printf ("r11 %016llx r12 %016llx r13 %016llx r14 %016llx\n",
			f->R.r11, f->R.r12, f->R.r13, f->R.r14);
	printf ("r15 %016llx rflags %08llx\n", f->R.r15, f->eflags);
	printf ("es: %04x ds: %04x cs: %04x ss: %04x\n",
			f->es, f->ds, f->cs, f->ss);
}

/* interrupt VEC의 이름을 반환한다. */
const char *
intr_name (uint8_t vec) {
	return intr_names[vec];
}
