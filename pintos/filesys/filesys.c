#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"

/* 파일 시스템을 담고 있는 디스크. */
struct disk *filesys_disk;

static void do_format (void);

/* 파일 시스템 모듈을 초기화한다.
 * FORMAT이 true이면 파일 시스템을 다시 포맷한다. */
void
filesys_init (bool format) {
	filesys_disk = disk_get (0, 1);
	if (filesys_disk == NULL)
		PANIC ("hd0:1 (hdb) not present, file system initialization failed");

	inode_init ();

#ifdef EFILESYS
	fat_init ();

	if (format)
		do_format ();

	fat_open ();
#else
	/* 원본 FS */
	free_map_init ();

	if (format)
		do_format ();

	free_map_open ();
#endif
}

/* 파일 시스템 모듈을 종료하고, 아직 기록되지 않은 데이터를
 * 디스크에 쓴다. */
void
filesys_done (void) {
	/* 원본 FS */
#ifdef EFILESYS
	fat_close ();
#else
	free_map_close ();
#endif
}

/* 주어진 INITIAL_SIZE로 이름이 NAME인 파일을 생성한다.
 * 성공하면 true를, 그렇지 않으면 false를 반환한다.
 * 이름이 NAME인 파일이 이미 존재하거나 내부 메모리 할당에 실패하면
 * 실패한다. */
bool
filesys_create (const char *name, off_t initial_size) {
	disk_sector_t inode_sector = 0;
	struct dir *dir = dir_open_root ();
	bool success = (dir != NULL
			&& free_map_allocate (1, &inode_sector)
			&& inode_create (inode_sector, initial_size)
			&& dir_add (dir, name, inode_sector));
	if (!success && inode_sector != 0)
		free_map_release (inode_sector, 1);
	dir_close (dir);

	return success;
}

/* 주어진 NAME의 파일을 연다.
 * 성공하면 새 파일을, 그렇지 않으면 null pointer를 반환한다.
 * 이름이 NAME인 파일이 없거나 내부 메모리 할당에 실패하면
 * 실패한다. */
struct file *
filesys_open (const char *name) {
	struct dir *dir = dir_open_root ();
	struct inode *inode = NULL;

	if (dir != NULL)
		dir_lookup (dir, name, &inode);
	dir_close (dir);

	return file_open (inode);
}

/* 이름이 NAME인 파일을 삭제한다.
 * 성공하면 true를, 실패하면 false를 반환한다.
 * 이름이 NAME인 파일이 없거나 내부 메모리 할당에 실패하면
 * 실패한다. */
bool
filesys_remove (const char *name) {
	struct dir *dir = dir_open_root ();
	bool success = dir != NULL && dir_remove (dir, name);
	dir_close (dir);

	return success;
}

/* 파일 시스템을 포맷한다. */
static void
do_format (void) {
	printf ("Formatting file system...");

#ifdef EFILESYS
	/* FAT를 생성해 디스크에 저장한다. */
	fat_create ();
	fat_close ();
#else
	free_map_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	free_map_close ();
#endif

	printf ("done.\n");
}
