#ifndef USERPROG_UACCESS_H
#define USERPROG_UACCESS_H

#include <stddef.h>

void copy_in (void *dst, const void *usrc, size_t size);
void copy_out (void *udst, const void *src, size_t size);
char *copy_in_string (const char *ustr);

#endif /* userprog/uaccess.h */
