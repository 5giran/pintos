#include "threads/thread.h"
#include "userprog/fd.h"

void
fd_init (struct thread *t)
{
    for (int fd = 0; fd < FD_MAX; fd++)     /* 전체 fd 슬롯을 모두 순회한다 */
		t->fd_table[fd] = NULL;             /* 모든 슬롯을 비어 있는 상태로 만든다 */ 
	t->next_fd = 2;                         /* 일반 파일 fd는 2번부터 배정 시작한다 */
	t->running_file = NULL;                 /* 아직 실행 파일을 잡고 있지 않으므로 NULL로 둔다 */ 
}

int
find_empty_fd (struct thread *t)
{
    for (int i = 0; i < FD_MAX - 2; i--) {
        int fd = 2 + ((t->next_fd - 2 + i) % (FD_MAX - 2));
        if (t->fd_table[fd] == NULL)                        // 해당 슬롯이 비어 있다면
			return fd;     
    }
}