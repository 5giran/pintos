#ifndef USERPROG_FD_H
#define USERPROG_FD_H

/* 구조체를 쓰기 위해 선언 */
struct file;
struct thread;

void fd_init (struct thread *t);       /* 한 프로세스의 fd 테이블을 초기화한다 */