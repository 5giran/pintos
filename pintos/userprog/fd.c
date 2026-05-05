#include "threads/thread.h"
#include "userprog/fd.h"
#include "filesys/file.h"

void
fd_init (struct thread *t)
{
    for (int fd = 0; fd < FD_MAX; fd++)     /* 전체 fd 슬롯을 모두 순회한다 */
		t->fd_table[fd] = NULL;             /* 모든 슬롯을 비어 있는 상태로 만든다 */ 
	t->next_fd = 2;                         /* 일반 파일 fd는 2번부터 배정 시작한다 */
	t->running_file = NULL;                 /* 아직 실행 파일을 잡고 있지 않으므로 NULL로 둔다 */ 
}

static int
find_empty_fd (struct thread *t)
{
    for (int i = 0; i < FD_MAX - 2; i++) {
        int fd = 2 + ((t->next_fd - 2 + i) % (FD_MAX - 2));
        if (t->fd_table[fd] == NULL)                        // 해당 슬롯이 비어 있다면
			return fd;     
    }
    return -1;
}

int
fd_alloc (struct file *file)
{
    struct thread *cur = thread_current ();
    int fd = find_empty_fd (cur);
    if (fd == -1)
        return -1;
    
    cur->fd_table[fd] = file;
    cur->next_fd = fd + 1;
    if (cur->next_fd >= FD_MAX)
        cur->next_fd = 2;
    return fd;
}

struct file *
fd_get (int fd)
{
    struct thread *cur = thread_current ();
    if (fd < 2 || fd >= FD_MAX)
        return NULL;
    return cur->fd_table[fd];
}

void 
fd_close (int fd)
{
    struct thread *cur = thread_current ();
	struct file *file;

	if (fd < 2 || fd >= FD_MAX)
		return;

	file = cur->fd_table[fd];
	if (file == NULL)
		return;

	cur->fd_table[fd] = NULL;

	lock_acquire (&filesys_lock);
	file_close (file);
	lock_release (&filesys_lock);
}

void
fd_close_all (void)
{
	for (int fd = 2; fd < FD_MAX; fd++)
		fd_close (fd);
}

bool
fd_duplicate_all (struct thread *dst, struct thread *src)
{
	for (int fd = 2; fd < FD_MAX; fd++) {
		struct file *src_file = src->fd_table[fd];
		struct file *dup;

		if (src_file == NULL)
			continue;

		lock_acquire (&filesys_lock);
		dup = file_duplicate (src_file);
		lock_release (&filesys_lock);

		if (dup == NULL) {
			for (int i = 2; i < fd; i++) {
				if (dst->fd_table[i] != NULL) {
					lock_acquire (&filesys_lock);
					file_close (dst->fd_table[i]);
					lock_release (&filesys_lock);
					dst->fd_table[i] = NULL;
				}
			}
			return false;
		}

		dst->fd_table[fd] = dup;
	}

	dst->next_fd = src->next_fd;
	return true;
}