#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"

/* system file inode의 sector들. */
#define FREE_MAP_SECTOR 0       /* free map 파일 inode sector. */
#define ROOT_DIR_SECTOR 1       /* root directory 파일 inode sector. */

/* file system에 사용하는 disk. */
extern struct disk *filesys_disk;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *name, off_t initial_size);
struct file *filesys_open (const char *name);
bool filesys_remove (const char *name);

#endif /* filesys/filesys.h */
