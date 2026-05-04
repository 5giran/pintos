#ifndef USERPROG_FD_H
#define USERPROG_FD_H

/* 구조체를 쓰기 위해 선언 */
struct file;
struct thread;

void fd_init (struct thread *t);            /* 한 프로세스의 fd 테이블을 초기화한다 */
// int find_empty_fd (struct thread *t);    /* fd 테이블에서 빈 자리를 찾는 함수 */    
int fd_alloc (struct file *file);           /* 열린 file에 새 fd를 배정하고 fd 번호를 반환 */
#endif                             // include guard 종료