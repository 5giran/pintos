#ifndef FILESYS_FILE_H
#define FILESYS_FILE_H

#include "filesys/off_t.h"

struct inode;

/* 파일 열기와 닫기. */
struct file *file_open (struct inode *);
struct file *file_reopen (struct file *);
struct file *file_duplicate (struct file *file);
void file_close (struct file *);
struct inode *file_get_inode (struct file *);

/* 읽기와 쓰기. */
off_t file_read (struct file *, void *, off_t);
off_t file_read_at (struct file *, void *, off_t size, off_t start);
off_t file_write (struct file *, const void *, off_t);
off_t file_write_at (struct file *, const void *, off_t size, off_t start);

/* 쓰기 방지. */
void file_deny_write (struct file *);
void file_allow_write (struct file *);

/* 파일 위치. */
void file_seek (struct file *, off_t);
off_t file_tell (struct file *);
off_t file_length (struct file *);

#endif /* filesys/file.h */
