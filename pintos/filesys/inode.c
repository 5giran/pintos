#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* inode를 식별합니다. */
#define INODE_MAGIC 0x494e4f44

/* 디스크 상의 inode입니다.
 * 정확히 DISK_SECTOR_SIZE 바이트여야 합니다. */
struct inode_disk {
	disk_sector_t start;                /* 첫 데이터 섹터. */
	off_t length;                       /* 파일 크기(바이트). */
	unsigned magic;                     /* 매직 번호. */
	uint32_t unused[125];               /* 사용하지 않습니다. */
};

/* SIZE 바이트 길이의 inode에 대해 할당할 섹터 수를 반환합니다. */
static inline size_t
bytes_to_sectors (off_t size) {
	return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* 메모리 내 inode. */
struct inode {
	struct list_elem elem;              /* inode 목록의 요소. */
	disk_sector_t sector;               /* 디스크 위치의 섹터 번호. */
	int open_cnt;                       /* 열려 있는 개수. */
	bool removed;                       /* 삭제되었으면 true, 아니면 false. */
	int deny_write_cnt;                 /* 0: 쓰기 허용, >0: 쓰기 금지. */
	struct inode_disk data;             /* inode 내용. */
};

/* INODE 안에서 바이트 오프셋 POS를 포함하는 디스크 섹터를 반환합니다.
 * INODE에 오프셋 POS의 바이트에 대한 데이터가 없으면 -1을 반환합니다. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) {
	ASSERT (inode != NULL);
	if (pos < inode->data.length)
		return inode->data.start + pos / DISK_SECTOR_SIZE;
	else
		return -1;
}

/* 단일 inode를 두 번 열어도 같은 `struct inode'를 반환하도록 하는 열려 있는 inode 목록. */
static struct list open_inodes;

/* inode 모듈을 초기화합니다. */
void
inode_init (void) {
	list_init (&open_inodes);
}

/* LENGTH 바이트의 데이터로 inode를 초기화하고
 * 새 inode를 파일 시스템 디스크의 섹터 SECTOR에 기록합니다.
 * 성공하면 true를 반환합니다.
 * 메모리 또는 디스크 할당에 실패하면 false를 반환합니다. */
bool
inode_create (disk_sector_t sector, off_t length) {
	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT (length >= 0);

	/* 이 assertion이 실패하면 inode 구조체의 크기가 정확히 한 섹터가 아니므로, 이를 수정해야 합니다. */
	ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

	disk_inode = calloc (1, sizeof *disk_inode);
	if (disk_inode != NULL) {
		size_t sectors = bytes_to_sectors (length);
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;
		if (free_map_allocate (sectors, &disk_inode->start)) {
			disk_write (filesys_disk, sector, disk_inode);
			if (sectors > 0) {
				static char zeros[DISK_SECTOR_SIZE];
				size_t i;

				for (i = 0; i < sectors; i++) 
					disk_write (filesys_disk, disk_inode->start + i, zeros); 
			}
			success = true; 
		} 
		free (disk_inode);
	}
	return success;
}

/* SECTOR에서 inode를 읽어 그 내용을 담은 `struct inode'를 반환합니다.
 * 메모리 할당에 실패하면 null pointer를 반환합니다. */
struct inode *
inode_open (disk_sector_t sector) {
	struct list_elem *e;
	struct inode *inode;

	/* 이 inode가 이미 열려 있는지 확인합니다. */
	for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
			e = list_next (e)) {
		inode = list_entry (e, struct inode, elem);
		if (inode->sector == sector) {
			inode_reopen (inode);
			return inode; 
		}
	}

	/* 메모리를 할당합니다. */
	inode = malloc (sizeof *inode);
	if (inode == NULL)
		return NULL;

	/* 초기화합니다. */
	list_push_front (&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
	disk_read (filesys_disk, inode->sector, &inode->data);
	return inode;
}

/* INODE를 다시 열어 반환합니다. */
struct inode *
inode_reopen (struct inode *inode) {
	if (inode != NULL)
		inode->open_cnt++;
	return inode;
}

/* INODE의 inode 번호를 반환합니다. */
disk_sector_t
inode_get_inumber (const struct inode *inode) {
	return inode->sector;
}

/* INODE를 닫고 디스크에 기록합니다.
 * 이것이 INODE에 대한 마지막 참조이면 메모리를 해제합니다.
 * INODE가 제거된 inode이기도 하면 해당 블록을 해제합니다. */
void
inode_close (struct inode *inode) {
	/* null 포인터는 무시합니다. */
	if (inode == NULL)
		return;

	/* 이것이 마지막 opener라면 자원을 해제합니다. */
	if (--inode->open_cnt == 0) {
		/* inode 목록에서 제거하고 잠금을 해제합니다. */
		list_remove (&inode->elem);

		/* 제거되었다면 블록을 해제합니다. */
		if (inode->removed) {
			free_map_release (inode->sector, 1);
			free_map_release (inode->data.start,
					bytes_to_sectors (inode->data.length)); 
		}

		free (inode); 
	}
}

/* INODE를 열고 있는 마지막 호출자가 닫을 때 삭제되도록 표시합니다. */
void
inode_remove (struct inode *inode) {
	ASSERT (inode != NULL);
	inode->removed = true;
}

/* OFFSET 위치부터 시작하여 INODE에서 BUFFER로 SIZE 바이트를 읽습니다.
 * 실제로 읽은 바이트 수를 반환하며, 오류가 발생하거나 파일 끝에 도달하면 SIZE보다 작을 수 있습니다. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) {
	uint8_t *buffer = buffer_;
	off_t bytes_read = 0;
	uint8_t *bounce = NULL;

	while (size > 0) {
		/* 읽을 디스크 섹터, 섹터 내 시작 바이트 오프셋. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* inode에 남은 바이트 수, 섹터에 남은 바이트 수, 둘 중 더 작은 값. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* 이 섹터에서 실제로 복사할 바이트 수. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* 전체 섹터를 호출자의 버퍼로 직접 읽어들입니다. */
			disk_read (filesys_disk, sector_idx, buffer + bytes_read); 
		} else {
			/* 섹터를 bounce buffer에 읽은 다음 호출자의 버퍼로 부분 복사합니다. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}
			disk_read (filesys_disk, sector_idx, bounce);
			memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
		}

		/* 다음으로 이동합니다. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}
	free (bounce);

	return bytes_read;
}

/* OFFSET 위치부터 시작하여 BUFFER의 SIZE 바이트를 INODE에 기록합니다.
 * 실제로 기록된 바이트 수를 반환하며, 파일 끝에 도달하거나 오류가 발생하면 SIZE보다 작을 수 있습니다.
 * (보통 파일 끝에서의 쓰기는 inode를 확장하지만,
 * 확장은 아직 구현되지 않았습니다.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
		off_t offset) {
	const uint8_t *buffer = buffer_;
	off_t bytes_written = 0;
	uint8_t *bounce = NULL;

	if (inode->deny_write_cnt)
		return 0;

	while (size > 0) {
		/* 기록할 디스크 섹터, 섹터 내 시작 바이트 오프셋. */
		disk_sector_t sector_idx = byte_to_sector (inode, offset);
		int sector_ofs = offset % DISK_SECTOR_SIZE;

		/* inode에 남은 바이트 수, 섹터에 남은 바이트 수, 둘 중 더 작은 값. */
		off_t inode_left = inode_length (inode) - offset;
		int sector_left = DISK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* 이 섹터에 실제로 기록할 바이트 수. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) {
			/* 전체 섹터를 디스크에 직접 기록합니다. */
			disk_write (filesys_disk, sector_idx, buffer + bytes_written); 
		} else {
			/* bounce buffer가 필요합니다. */
			if (bounce == NULL) {
				bounce = malloc (DISK_SECTOR_SIZE);
				if (bounce == NULL)
					break;
			}

			/* 섹터에 우리가 기록하는 청크 앞이나 뒤에 데이터가 있으면, 먼저 그 섹터를 읽어 와야 합니다. 그렇지 않으면 모든 바이트가 0인
			   섹터에서 시작합니다. */
			if (sector_ofs > 0 || chunk_size < sector_left) 
				disk_read (filesys_disk, sector_idx, bounce);
			else
				memset (bounce, 0, DISK_SECTOR_SIZE);
			memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
			disk_write (filesys_disk, sector_idx, bounce); 
		}

		/* 다음으로 이동합니다. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
	}
	free (bounce);

	return bytes_written;
}

/* INODE에 대한 쓰기를 비활성화합니다.
   inode opener당 최대 한 번만 호출할 수 있습니다. */
	void
inode_deny_write (struct inode *inode) 
{
	inode->deny_write_cnt++;
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* INODE에 대한 쓰기를 다시 활성화합니다.
 * inode에서 `inode_deny_write()`를 호출한 각 inode opener는
 * inode를 닫기 전에 한 번씩 이 함수를 호출해야 합니다. */
void
inode_allow_write (struct inode *inode) {
	ASSERT (inode->deny_write_cnt > 0);
	ASSERT (inode->deny_write_cnt <= inode->open_cnt);
	inode->deny_write_cnt--;
}

/* INODE 데이터의 길이를 바이트 단위로 반환합니다. */
off_t
inode_length (const struct inode *inode) {
	return inode->data.length;
}
