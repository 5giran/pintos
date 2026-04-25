#ifndef DEVICES_DISK_H
#define DEVICES_DISK_H

#include <inttypes.h>
#include <stdint.h>

/* disk sector의 크기(바이트 단위). */
#define DISK_SECTOR_SIZE 512

/* disk 내부 sector의 index.
 * 2 TB까지의 disk에는 충분하다. */
typedef uint32_t disk_sector_t;

/* printf()용 format specifier, 예:
 * printf ("sector=%"PRDSNu"\n", sector); */
#define PRDSNu PRIu32

void disk_init (void);
void disk_print_stats (void);

struct disk *disk_get (int chan_no, int dev_no);
disk_sector_t disk_size (struct disk *);
void disk_read (struct disk *, disk_sector_t, void *);
void disk_write (struct disk *, disk_sector_t, const void *);

void 	register_disk_inspect_intr ();
#endif /* devices/disk.h */
