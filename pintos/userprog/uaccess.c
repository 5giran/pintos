#include "userprog/uaccess.h"
#include <string.h>
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

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
