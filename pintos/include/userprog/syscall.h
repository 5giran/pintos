#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"
#include <stddef.h>

extern struct lock filesys_lock;

void syscall_init (void);
void validate_user_read (const void *buffer, size_t size);
void validate_user_write (const void *buffer, size_t size);
void copy_in (void *dst, const void *usrc, size_t size);
void copy_out (void *udst, const void *src, size_t size);
char *copy_in_string (const char *ustr);

void validate_user_read(const void *buffer, size_t size);
void validate_user_write(const void *buffer, size_t size);


void copy_in (void *dst, const void *usrc, size_t size);
void copy_out (void *udst, const void *src, size_t size);
char *copy_in_string (const char *ustr);

#endif /* userprog/syscall.h */
