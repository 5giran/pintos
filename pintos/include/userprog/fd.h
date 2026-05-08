#ifndef USERPROG_FD_H
#define USERPROG_FD_H

#include <stdbool.h>

/* 구조체를 쓰기 위해 선언 */
struct file;
struct thread;

void fd_init (struct thread *t);            /* 한 프로세스의 fd 테이블을 초기화한다 */
// int find_empty_fd (struct thread *t);    /* fd 테이블에서 빈 자리를 찾는 함수 */    
int fd_alloc (struct file *file);           /* 열린 file에 새 fd를 배정하고 fd 번호를 반환 */
struct file *fd_get (int fd);               /* fd 번호로 대응되는 file 포인터를 가져온다 */
void fd_close (int fd);
void fd_close_all (void);
bool fd_duplicate_all (struct thread *dst, struct thread *src);
#endif                             // include guard 종료