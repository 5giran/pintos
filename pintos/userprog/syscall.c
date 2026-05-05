#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/kernel/stdio.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/palloc.h"
#include "userprog/fd.h"
#include "userprog/process.h"
#include "userprog/uaccess.h"
#include "intrinsic.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);
/* 책임 분리랑 테스트 일부 미통과 되어가지규 빼구 process.c에 다시 짜야할 거 같습니당.. */
/* 테스트 때문에 일단 넣어둘게여 */
static void exit_with_status (int status);
// 얘는 이제 더 이상 쓰진 않을 거 같슴다. UACCESS 때문에...
// static void check_address (const void *addr);

struct lock filesys_lock;

static bool sys_create (const char *ufile, unsigned initial_size);
static bool sys_remove (const char *ufile);
static int sys_open (const char *ufile);
static int sys_filesize (int fd);
static int sys_read (int fd, void *ubuf, unsigned size);
static int sys_write (int fd, const void *ubuf, unsigned size);
static void sys_seek (int fd, unsigned position);
static unsigned sys_tell (int fd);
static void sys_close (int fd);

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
	lock_init (&filesys_lock);
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
			process ((int) f->R.rdi);
			break;

		case SYS_CREATE:
			f->R.rax = sys_create ((const char*) f->R.rdi, 
								   (unsigned) f->R.rsi);
			break;

		case SYS_REMOVE:
			f->R.rax = sys_remove ((const char*) f->R.rdi);
			break;

		case SYS_OPEN:
			f->R.rax = sys_open ((const char*) f->R.rdi);
			break;

		case SYS_FILESIZE:
			f->R.rax = sys_filesize ((int) f->R.rdi);
			break;

		case SYS_READ:
			f->R.rax = sys_read ((int) f->R.rdi, 
								 (void *) f->R.rsi,
								 (unsigned) f->R.rdx);
			break;

		case SYS_WRITE:
			f->R.rax = sys_read ((int) f->R.rdi, 
								 (const void *) f->R.rsi,
								 (unsigned) f->R.rdx);
			break;

		case SYS_SEEK:
			sys_seek ((int) f->R.rdi,
					  (unsigned) f->R.rsi);
			break;
			
		case SYS_TELL:
			f->R.rax = sys_tell ((int) f->R.rdi);
			break;

		case SYS_CLOSE:
			sys_close ((int) f->R.rdi);
			break;

		/* 아직 구현 안 한 syscall은 비정상 종료 */
		default:
			exit_with_status (-1);
			break;
	}
}

static void
exit_with_status (int status) 
{
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

static bool 
sys_create (const char *ufile, unsigned initial_size)
{
	char *kfile = copy_in_string (ufile);
	bool ok;

	lock_acquire (&filesys_lock);
	ok = filesys_create (kfile, initial_size);
	lock_release (&filesys_lock);

	palloc_free_page (kfile);
	return ok;
}

static bool 
sys_remove (const char *ufile)
{
	char *kfile = copy_in_string (ufile);
	bool ok;

	lock_acquire (&filesys_lock);
	ok = filesys_remove (kfile);
	lock_release (&filesys_lock);

	palloc_free_page (kfile);
	return ok;
}


static int 
sys_open (const char *ufile)
{
	char *kfile = copy_in_string (ufile);
	struct file *file;
	int fd;

	lock_acquire (&filesys_lock);
	file = filesys_open (kfile);
	lock_release (&filesys_lock);

	palloc_free_page (kfile);

	if (file == NULL) {
		return -1;
	}

	fd = fd_alloc (file);
	if (fd == -1) {
		lock_acquire (&filesys_lock);
		file_close (file);
		lock_release (&filesys_lock);
	}
}

static int 
sys_filesize (int fd)
{
	struct file *file = fd_get (fd);
	int len;

	if (file == NULL) {
		return -1;
	}

	lock_acquire (&filesys_lock);
	len = (int) file_length (file);
	lock_release (&filesys_lock);

	return len;
}

static int 
sys_read (int fd, void *ubuf, unsigned size)
{
uint8_t *kbuf;
	unsigned total = 0;

	if (size == 0)
		return 0;

	validate_user_write (ubuf, size);

	if (fd == 1)
		return -1;

	kbuf = palloc_get_page (0);
	if (kbuf == NULL)
		return -1;

	while (total < size) {
		unsigned chunk = size - total > PGSIZE ? PGSIZE : size - total;
		int bytes = 0;

		if (fd == 0) {
			for (unsigned i = 0; i < chunk; i++)
				kbuf[i] = input_getc ();
			bytes = (int) chunk;
		} else {
			struct file *file = fd_get (fd);
			if (file == NULL) {
				palloc_free_page (kbuf);
				return -1;
			}

			lock_acquire (&filesys_lock);
			bytes = (int) file_read (file, kbuf, chunk);
			lock_release (&filesys_lock);
		}

		if (bytes <= 0)
			break;

		copy_out ((uint8_t *) ubuf + total, kbuf, (size_t) bytes);
		total += (unsigned) bytes;

		if ((unsigned) bytes < chunk)
			break;
	}

	palloc_free_page (kbuf);
	return (int) total;
}

static int 
sys_write (int fd, const void *ubuf, unsigned size)
{
	uint8_t *kbuf;
	unsigned total = 0;

	if (size == 0) {
		return 0;
	}

	validate_user_read (ubuf, size);

	if (fd == 0) {
		return -1;
	}

	kbuf = palloc_get_page (0);
	if (kbuf == NULL) {
		return -1;
	}

	while (total < size) {
		unsigned chunk = size - total > PGSIZE ? PGSIZE : size - total;
		int written;
	
		copy_in (kbuf, (const uint8_t *) ubuf + total, chunk);

		if (fd == 1) {
			putbuf ((char *) kbuf, chunk);
			total += chunk;
			continue;
		}

		{
			struct file *file = fd_get (fd);
			if (file == NULL) {
				palloc_free_page (kbuf);
				return -1;
			}

			lock_acquire (&filesys_lock);
			written = (int) file_write (file, kbuf, chunk);
			lock_release (&filesys_lock);
		}

		if (written <= 0) {
			break;
		}
	}
	palloc_free_page (kbuf);
	return (int) total;
}

static void 
sys_seek (int fd, unsigned position)
{
	struct file *file = fd_get (fd);

	if (file == NULL) {
		return;
	}

	lock_acquire (&filesys_lock);
	file_seek (file, (off_t) position);
	lock_release (&filesys_lock);
}

static unsigned 
sys_tell (int fd)
{
	struct file *file = fd_get (fd);
	unsigned pos;

	if (file == NULL) {
		return (unsigned) -1;
	}

	lock_acquire (&filesys_lock);
	pos = (unsigned) file_tell (file);
	lock_release (&filesys_lock);

	return pos;
}

static void 
sys_close (int fd)
{
	fd_close (fd);
}