#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "threads/init.h"      // power_off 선언 있음
#include "threads/vaddr.h"     // is_user_vaddr
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
static void exit_with_status (int status);
static void check_address (const void *addr);

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
	/* syscall 진입 시 사용할 코드 세그먼트 정보 설정 */
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	/* syscall 명령이 들어오면 어디로 점프할지 설정 */
	write_msr (MSR_LSTAR, (uint64_t) syscall_entry);
	/* syscall_entry가 userland stack을 kernel mode stack으로 바꿀 때까지 interrupt
	 * service routine은 어떤 interrupt도 처리해서는 안 된다. 따라서 FLAG_FL을 마스킹했다. */
	/* syscall 진입 중 마스킹할 플래그 설정 */
	write_msr (MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* 주요 system call interface */
/* 인터럽트나 시스템 콜이 발생했을 때 cpu 레지스터 상태를 저장한 구조체 */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: 구현을 여기에 작성하라.
	/* rax에 syscall 번호가 들어 있다. */
	switch (f->R.rax) {

		/* 시스템 종료 */
		case SYS_HALT:
			power_off ();
			break;

		/* 현재 프로세스 종료 */
		case SYS_EXIT:
			exit_with_status ((int) f->R.rdi);
			break;

		/* write(fd, buffer, size) */
		case SYS_WRITE: {
			/* 첫 번째 인자 fd는 rdi */
			int fd = (int) f->R.rdi;

			/* 두 번째 인자 buffer는 rsi */
			const void *buffer = (const void *) f->R.rsi;

			/* 세 번째 인자 size는 rdx */
			unsigned size = (unsigned) f->R.rdx;

			/* size > 0 이면 시작 주소 검증 */
			if (size > 0) {
				check_address (buffer);

				/* 버퍼의 마지막 바이트 주소도 검증 */
				check_address ((const uint8_t *) buffer + size - 1);
			}

			/* fd == 1 은 stdout */
			if (fd == 1) {
				/* 콘솔에 버퍼 내용을 출력 */
				putbuf (buffer, size);

				/* 반환값 = 실제 쓴 바이트 수 */
				f->R.rax = size;
			} else {
				/* 지금은 stdout만 지원 */
				f->R.rax = -1;
			}
			break;
		}

		/* 아직 구현 안 한 syscall은 비정상 종료 */
		default:
			exit_with_status (-1);
			break;
	}
}

static void
exit_with_status (int status) {
	/* 종료 메시지 출력 */
	printf ("%s: exit(%d)\n", thread_name (), status);

	/* 현재 스레드 종료 */
	thread_exit ();
}

static void
check_address (const void *addr) {
	/* NULL 이거나 유저 가상주소 범위 밖이면 비정상 종료 */
	if (addr == NULL || !is_user_vaddr (addr))
		exit_with_status (-1);
}