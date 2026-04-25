#include "filesys/file.h"
#include <debug.h>
#include "filesys/inode.h"
#include "threads/malloc.h"

/* 열려 있는 파일. */
struct file {
	struct inode *inode;        /* 파일의 inode. */
	off_t pos;                  /* 현재 위치. */
	bool deny_write;            /* file_deny_write()가 호출되었는가? */
};

/* 주어진 INODE에 대한 파일을 열고, 소유권을 가져오며,
 * 새 파일을 반환한다. 할당에 실패하거나 INODE가 null이면
 * null pointer를 반환한다. */
struct file *
file_open (struct inode *inode) {
	struct file *file = calloc (1, sizeof *file);
	if (inode != NULL && file != NULL) {
		file->inode = inode;
		file->pos = 0;
		file->deny_write = false;
		return file;
	} else {
		inode_close (inode);
		free (file);
		return NULL;
	}
}

/* FILE과 같은 inode를 사용하는 새 파일을 열어 반환한다.
 * 실패하면 null pointer를 반환한다. */
struct file *
file_reopen (struct file *file) {
	return file_open (inode_reopen (file->inode));
}

/* attributes를 포함한 file object를 복제하고, FILE과 같은 inode를 사용하는
 * 새 파일을 반환한다. 실패하면 null pointer를 반환한다. */
struct file *
file_duplicate (struct file *file) {
	struct file *nfile = file_open (inode_reopen (file->inode));
	if (nfile) {
		nfile->pos = file->pos;
		if (file->deny_write)
			file_deny_write (nfile);
	}
	return nfile;
}

/* FILE을 닫는다. */
void
file_close (struct file *file) {
	if (file != NULL) {
		file_allow_write (file);
		inode_close (file->inode);
		free (file);
	}
}

/* FILE에 캡슐화된 inode를 반환한다. */
struct inode *
file_get_inode (struct file *file) {
	return file->inode;
}

/* FILE의 현재 위치에서 BUFFER로 SIZE 바이트를 읽는다.
 * 실제로 읽은 바이트 수를 반환하며, end of file에 도달하면 SIZE보다 적을 수 있다.
 * 읽은 바이트 수만큼 FILE의 위치를 앞으로 옮긴다. */
off_t
file_read (struct file *file, void *buffer, off_t size) {
	off_t bytes_read = inode_read_at (file->inode, buffer, size, file->pos);
	file->pos += bytes_read;
	return bytes_read;
}

/* FILE의 offset FILE_OFS에서 BUFFER로 SIZE 바이트를 읽는다.
 * 실제로 읽은 바이트 수를 반환하며, end of file에 도달하면 SIZE보다 적을 수 있다.
 * FILE의 현재 위치는 영향을 받지 않는다. */
off_t
file_read_at (struct file *file, void *buffer, off_t size, off_t file_ofs) {
	return inode_read_at (file->inode, buffer, size, file_ofs);
}

/* FILE의 현재 위치에서 시작하여 BUFFER의 SIZE 바이트를 FILE에 쓴다.
 * 실제로 쓴 바이트 수를 반환하며, end of file에 도달하면 SIZE보다 적을 수 있다.
 * (보통 그런 경우에는 파일을 늘리겠지만, file growth는 아직 구현되지 않았다.)
 * 읽은 바이트 수만큼 FILE의 위치를 앞으로 옮긴다. */
off_t
file_write (struct file *file, const void *buffer, off_t size) {
	off_t bytes_written = inode_write_at (file->inode, buffer, size, file->pos);
	file->pos += bytes_written;
	return bytes_written;
}

/* FILE의 offset FILE_OFS에서 시작하여 BUFFER의 SIZE 바이트를 FILE에 쓴다.
 * 실제로 쓴 바이트 수를 반환하며, end of file에 도달하면 SIZE보다 적을 수 있다.
 * (보통 그런 경우에는 파일을 늘리겠지만, file growth는 아직 구현되지 않았다.)
 * FILE의 현재 위치는 영향을 받지 않는다. */
off_t
file_write_at (struct file *file, const void *buffer, off_t size,
		off_t file_ofs) {
	return inode_write_at (file->inode, buffer, size, file_ofs);
}

/* file_allow_write()가 호출되거나 FILE이 닫힐 때까지 FILE의 기반 inode에 대한 쓰기 작업을 막는다. */
void
file_deny_write (struct file *file) {
	ASSERT (file != NULL);
	if (!file->deny_write) {
		file->deny_write = true;
		inode_deny_write (file->inode);
	}
}

/* FILE의 기반 inode에 대한 쓰기 작업을 다시 허용한다.
 * (같은 inode를 열어 둔 다른 file 때문에 여전히 쓰기가 거부될 수 있다.) */
void
file_allow_write (struct file *file) {
	ASSERT (file != NULL);
	if (file->deny_write) {
		file->deny_write = false;
		inode_allow_write (file->inode);
	}
}

/* FILE의 크기를 바이트 단위로 반환한다. */
off_t
file_length (struct file *file) {
	ASSERT (file != NULL);
	return inode_length (file->inode);
}

/* FILE의 현재 위치를 파일 시작점에서 NEW_POS 바이트 떨어진 위치로 설정한다. */
void
file_seek (struct file *file, off_t new_pos) {
	ASSERT (file != NULL);
	ASSERT (new_pos >= 0);
	file->pos = new_pos;
}

/* FILE의 현재 위치를 파일 시작점으로부터의 바이트 오프셋으로 반환한다. */
off_t
file_tell (struct file *file) {
	ASSERT (file != NULL);
	return file->pos;
}
