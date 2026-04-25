#include "threads/loader.h"

.text
.globl syscall_entry
.type syscall_entry, @function
syscall_entry:
	movq %rbx, temp1(%rip)
	movq %r12, temp2(%rip)     /* callee-saved 레지스터 */
	movq %rsp, %rbx            /* userland rsp 저장 */
	movabs $tss, %r12
	movq (%r12), %r12
	movq 4(%r12), %rsp         /* ring0 rsp를 tss에서 읽는다 */
	/* 이제 커널 스택에 있다 */
	push $(SEL_UDSEG)      /* if->ss */
	push %rbx              /* if->rsp */
	push %r11              /* if->eflags */
	push $(SEL_UCSEG)      /* if->cs */
	push %rcx              /* if->rip */
	subq $16, %rsp         /* error_code, vec_no를 건너뛴다 */
	push $(SEL_UDSEG)      /* if->ds */
	push $(SEL_UDSEG)      /* if->es */
	push %rax
	movq temp1(%rip), %rbx
	push %rbx
	pushq $0
	push %rdx
	push %rbp
	push %rdi
	push %rsi
	push %r8
	push %r9
	push %r10
	pushq $0 /* r11을 건너뛴다 */
	movq temp2(%rip), %r12
	push %r12
	push %r13
	push %r14
	push %r15
	movq %rsp, %rdi

check_intr:
	btsq $9, %r11          /* 인터럽트를 복구할 수 있는지 확인한다 */
	jnb no_sti
	sti                    /* 인터럽트를 복원한다 */
no_sti:
	movabs $syscall_handler, %r12
	call *%r12
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rsi
	popq %rdi
	popq %rbp
	popq %rdx
	popq %rcx
	popq %rbx
	popq %rax
	addq $32, %rsp
	popq %rcx              /* if->rip */
	addq $8, %rsp
	popq %r11              /* if->eflags */
	popq %rsp              /* if->rsp */
	sysretq

.section .data
.globl temp1
temp1:
.quad	0
.globl temp2
temp2:
.quad	0
