#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* 시스템 콜.
 *
 * 이전에는 system call 서비스가 interrupt handler(예: linux의 int 0x80)에 의해 처리되었다. 그러나
 * x86-64에서는 제조사가 system call 요청을 위한 효율적인 경로인 `syscall` instruction을 제공한다.
 *
 * syscall instruction은 Model Specific Register (MSR)의 값을 읽는 방식으로 동작한다. 자세한
 * 내용은 manual을 참고하라. */

#define MSR_STAR 0xc0000081         /* 세그먼트 셀렉터 MSR */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL 대상 */
#define MSR_SYSCALL_MASK 0xc0000084 /* eflags용 마스크 */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* syscall_entry가 userland stack을 kernel mode stack으로 바꿀 때까지 interrupt
	 * service routine은 어떤 interrupt도 처리해서는 안 된다. 따라서 FLAG_FL을 마스킹했다. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* 주요 system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: 구현을 여기에 작성하라.
	printf ("system call!\n");
	thread_exit ();
}
