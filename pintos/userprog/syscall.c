#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "threads/init.h"      // power_off 선언 있음
#include "threads/vaddr.h"     // is_user_vaddr
#include "threads/mmu.h"
#include "intrinsic.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"

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
syscall_init (void) 
{
	/* syscall 진입 시 사용할 코드 세그먼트 정보 설정 */
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
			
	/* syscall 명령이 들어오면 어디로 점프할지 설정 */
	write_msr (MSR_LSTAR, (uint64_t) syscall_entry);
	
	/* syscall_entry가 userland stack을 kernel mode stack으로 바꿀 때까지 interrupt
	 * service routine은 어떤 interrupt도 처리해서는 안 된다. 따라서 FLAG_FL을 마스킹했다.
	 * syscall 진입 중 마스킹할 플래그 설정 */
	write_msr (MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* 주요 system call interface */
/* 인터럽트나 시스템 콜이 발생했을 때 cpu 레지스터 상태를 저장한 구조체가 intr_frame */
/* 시스템콜 인자들이 syscall_handler에 일반 함수 인자처럼 들어오는 게 아니라, 
 * f 안에 저장된 레지스터 값으로 들어온다. */
void
syscall_handler (struct intr_frame *f UNUSED) 
{
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
		
		/* 현재 파일 열기 */
		case SYS_OPEN: {
			/* 유저 프로그램이 넘긴 첫 번째 인자(file 이름 주소)는 rdi에 들어 있다. */
			const char *user_file = (const char *) f->R.rdi;

			/* 유저 주소의 문자열을 커널이 안전하게 쓸 수 있는 커널 문자열로 복사한다. */
			char *file_name = copy_in_string (user_file);

			/* 파일 시스템 내부 자료구조 보호를 위해 filesys_lock을 잡는다. */
			// lock_acquire (&filesys_lock);

			/* 복사된 커널 문자열을 이용해 실제 파일을 연다. */
			struct file *opened_file = filesys_open (file_name);

			/* filesys_open() 호출이 끝났으므로 filesys_lock을 푼다. */
			// lock_release (&filesys_lock);

			/* 파일 열기에 실패한 경우 open()의 반환값은 -1이다. */
			if (opened_file == NULL) {
				f->R.rax = -1;
			} else {
				/* 열린 파일 객체를 현재 프로세스의 fd table에 등록하고 fd 번호를 받는다. */
				int fd = fd_alloc (opened_file);

				/* fd table이 가득 찼거나 등록에 실패한 경우 fd_alloc()은 -1을 반환한다. */
				if (fd < 0) {
					/* fd 등록 실패 시 방금 연 파일 객체를 닫아서 자원 누수를 막는다. */
					// lock_acquire (&filesys_lock);
					file_close (opened_file);
					// lock_release (&filesys_lock);
				}

				/* 성공하면 2 이상의 fd, 실패하면 -1을 유저 프로그램에 반환한다. */
				f->R.rax = fd;
			}

			/* copy_in_string()이 할당한 커널 문자열 버퍼를 해제한다. */
			palloc_free_page (file_name);

			/* SYS_OPEN 처리를 끝내고 switch 문을 빠져나간다. */
			break;
		}



		/* write(fd, buffer, size) */
		case SYS_WRITE: {
			/* 첫 번째 인자 fd는 rdi */
			int fd = (int) f->R.rdi;

			/* 두 번째 인자 buffer는 rsi */
			const void *buffer = (const void *) f->R.rsi;

			/* 세 번째 인자 size는 rdx */
			unsigned size = (unsigned) f->R.rdx;

			/* 시작 주소 검증 */
			// SYS_WRITE: 유저 관점, validate_user_read: 커널 관점
			// 유저가 buffer에 WRITE 하려면 그 buffer가 WRITE할 수 있는 상태인지 커널이 read 해봐야함.
			validate_user_read(buffer, size);

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

		/* read(fd, buffer, size) */
		case SYS_READ: {
			/* 첫 번째 인자 fd는 rdi */
			int fd = (int) f->R.rdi;

			/* 두 번째 인자 buffer는 rsi */
			// user buffer에서 써야해서 수정가능해야함
			void *buffer = (void *) f->R.rsi;

			/* 세 번째 인자 size는 rdx */
			unsigned size = (unsigned) f->R.rdx;

			/* 시작 주소 검증 */
			validate_user_write(buffer, size);

			/* fd == 0 은 stdin */
			if (fd == 0) {
				/* buffer를 1바이트씩 쓸 수 있는 포인터로 */
				uint8_t *bf = (uint8_t *) buffer;

				/* stdin에서 한 글자씩 읽어서 user buffer에 채움 */
				for (size_t i = 0; i < size; i++) {
					bf[i] = input_getc ();
				}

				/* 반환값 = 실제 읽은 바이트 수 */
				f->R.rax = size;
			} else {
				/* 지금은 stdin만 지원 */
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
exit_with_status (int status) 
{
	// wait 구현 할 떄 child process 구조체 내부에 int exit_status 멤버 만든 뒤 상태 저장 해야함

	/* 종료 메시지 출력 */
	printf ("%s: exit(%d)\n", thread_name (), status);

	/* 현재 스레드 종료 */
	thread_exit ();
}

// static void
// check_address (const void *addr) 
// {
// 	/* NULL 이거나 유저 가상주소 범위 밖이면 비정상 종료 */
// 	if (addr == NULL || !is_user_vaddr (addr))
// 		exit_with_status (-1);
// }


// t/f을 돌려주지 않고, 그 자리에서 프로세스를 죽이는 함수여야 공통규약에 맞음.
// exit_with_status(-1): 현재 프로세스 바로 종료
void
validate_user_read(const void *buffer, size_t size)
{
    if (size == 0) return;
    if (buffer == NULL) exit_with_status(-1); // size > 0인데 주소가 없다, 메모리 접근 불가.

	// 반복문 돌리기 위해서 주소를 1바이트씩 이동시킬 수 있게 변경: buffer의 시작 바이트 주소
    const uint8_t *bf =(const uint8_t *)buffer;
	// end address 따로 정의: buffer의 끝 바이트 주소
	const uint8_t *end_adr = bf+size-1;

	// 끝 주소 = 시작 주소보다 같거나 커야 한다, 근데 시작 주소보다 작다 (초과분이 잘린거임)
	if (end_adr < bf) exit_with_status(-1); // 사이즈가 말도 안되게 큰 경우, 주소 오버플로우 처리

	// 순회할 시작페이지, 끝 페이지 정의
	const uint8_t *start_page = pg_round_down(bf);
	const uint8_t *end_page = pg_round_down(end_adr);

    // buffer가 걸쳐진 모든 page를 순회 (페이지 단위로 확인)
    // buffer의 시작주소 + PGSIZE: buffer의 끝 주소까지 순회
    for (const uint8_t *i = start_page; i <= end_page; i+=PGSIZE) {
		// page 시작주소가 user virtual address 범위 안에 있지 않다면 현재 프로세스 exit(-1)
		if (!is_user_vaddr(i)) exit_with_status(-1);
		// page가 실제 mapped 되어있지 않다면 현재 프로세스 exit(-1)
		// pml4_get_page 함수 인수 확인, thread_current()->pml4: 현재 실행중인 함수 인수 테이블
		if (!pml4_get_page(thread_current()->pml4, i)) exit_with_status(-1);
	}
}


void
validate_user_write(const void *buffer, size_t size)
{
	if (size == 0) return;
    if (buffer == NULL) exit_with_status(-1); // size > 0인데 주소가 없다, 메모리 접근 불가. 바로 false

	// 반복문 돌리기 위해서 주소를 1바이트씩 이동시킬 수 있게 변경: buffer의 시작 바이트 주소
    const uint8_t *bf = (const uint8_t *)buffer;
	// end address 따로 정의: buffer의 끝 바이트 주소
	const uint8_t *end_adr = bf+size-1;

	// 끝 주소 = 시작 주소보다 같거나 커야 한다, 근데 시작 주소보다 작다 (초과분이 잘린거임)
	if (end_adr < bf) exit_with_status(-1); // 사이즈가 말도 안되게 큰 경우, 주소 오버플로우 처리

	// 순회할 시작페이지, 끝 페이지 정의
	const uint8_t *start_page = pg_round_down(bf);
	const uint8_t *end_page = pg_round_down(end_adr);

    // buffer가 걸쳐진 모든 page를 순회 (페이지 단위로 확인)
    // buffer의 시작주소 + PGSIZE: buffer의 끝 주소까지 순회
    for (const uint8_t *i = start_page; i <= end_page; i+=PGSIZE) {
		// page 시작주소가 user virtual address 범위 안에 있지 않다면 현재 프로세스 exit(-1)
		if (!is_user_vaddr (i)) exit_with_status (-1);
		// writable 페이지인지 검증 - PTE 사용
		// pte가 NULL이 아닌지, *pte에 PTE_P가 있는지, *pte에 PTE_U가 있는지,  *pte에 PTE_W가 있는지 - 모두 충족해야함
		uint64_t *pte = pml4e_walk (thread_current ()->pml4, (const uint64_t)i, false);

		if (pte == NULL || (*pte & PTE_P) == 0 || (*pte & PTE_U) == 0)
			exit_with_status (-1);

		if ((*pte & PTE_W) == 0)
			exit_with_status (-1);
    }
}



void
copy_in (void *dst, const void *usrc, size_t size)
{
	/* TODO: user memory -> kernel memory */
}

void
copy_out (void *udst, const void *src, size_t size)
{
	/* TODO: kernel memory -> user memory */
}

char *
copy_in_string (const char *ustr)
{
	/* TODO: user string을 kernel buffer로 복사 */
	return NULL;
}