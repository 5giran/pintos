#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

/* 디렉터리. */
struct dir {
	struct inode *inode;                /* backing store(백킹 스토어). */
	off_t pos;                          /* 현재 위치. */
};

/* 단일 디렉터리 엔트리. */
struct dir_entry {
	disk_sector_t inode_sector;         /* 헤더의 섹터 번호. */
	char name[NAME_MAX + 1];            /* 널 종료 파일 이름. */
	bool in_use;                        /* 사용 중입니까, 아니면 비어 있습니까? */
};

/* 주어진 SECTOR에 ENTRY_CNT개의 항목을 위한 공간이 있는
 * 디렉터리를 생성합니다. 성공하면 true, 실패하면 false를 반환합니다. */
bool
dir_create (disk_sector_t sector, size_t entry_cnt) {
	return inode_create (sector, entry_cnt * sizeof (struct dir_entry));
}

/* 주어진 INODE에 대한 디렉터리를 열어 반환하며, 해당 INODE의
 * 소유권을 가져옵니다. 실패하면 null 포인터를 반환합니다. */
struct dir *
dir_open (struct inode *inode) {
	struct dir *dir = calloc (1, sizeof *dir);
	if (inode != NULL && dir != NULL) {
		dir->inode = inode;
		dir->pos = 0;
		return dir;
	} else {
		inode_close (inode);
		free (dir);
		return NULL;
	}
}

/* 루트 디렉터리를 열어 그에 대한 디렉터리를 반환합니다.
 * 성공하면 true, 실패하면 false를 반환합니다. */
struct dir *
dir_open_root (void) {
	return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* DIR과 같은 inode에 대한 새 디렉터리를 열어 반환합니다.
 * 실패하면 null 포인터를 반환합니다. */
struct dir *
dir_reopen (struct dir *dir) {
	return dir_open (inode_reopen (dir->inode));
}

/* DIR을 파괴하고 관련 리소스를 해제한다. */
void
dir_close (struct dir *dir) {
	if (dir != NULL) {
		inode_close (dir->inode);
		free (dir);
	}
}

/* DIR에 캡슐화된 inode를 반환한다. */
struct inode *
dir_get_inode (struct dir *dir) {
	return dir->inode;
}

/* DIR에서 주어진 NAME을 가진 파일을 찾는다.
 * 성공하면 true를 반환하고, EP가 null이 아니면 *EP를 디렉터리 엔트리로 설정하며, OFSP가 null이 아니면 *OFSP를
 * 디렉터리 엔트리의 바이트 오프셋으로 설정한다.
 * 그렇지 않으면 false를 반환하고 EP와 OFSP는 무시한다. */
static bool
lookup (const struct dir *dir, const char *name,
		struct dir_entry *ep, off_t *ofsp) {
	struct dir_entry e;
	size_t ofs;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
			ofs += sizeof e)
		if (e.in_use && !strcmp (name, e.name)) {
			if (ep != NULL)
				*ep = e;
			if (ofsp != NULL)
				*ofsp = ofs;
			return true;
		}
	return false;
}

/* DIR에서 주어진 NAME을 가진 파일을 찾고,
 * 있으면 true를, 없으면 false를 반환한다.
 * 성공하면 *INODE를 해당 파일의 inode로 설정하고, 그렇지 않으면
 * null pointer로 설정한다. 호출자는 *INODE를 닫아야 한다. */
bool
dir_lookup (const struct dir *dir, const char *name,
		struct inode **inode) {
	struct dir_entry e;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	if (lookup (dir, name, &e, NULL))
		*inode = inode_open (e.inode_sector);
	else
		*inode = NULL;

	return *inode != NULL;
}

/* NAME이라는 이름의 파일을 DIR에 추가한다. DIR에는 이미 같은 이름의
 * 파일이 있으면 안 된다. 파일의 inode는 INODE_SECTOR에 있다.
 * 성공하면 true를, 실패하면 false를 반환한다.
 * NAME이 유효하지 않거나(즉, 너무 길거나) disk 또는 memory
 * error가 발생하면 실패한다. */
bool
dir_add (struct dir *dir, const char *name, disk_sector_t inode_sector) {
	struct dir_entry e;
	off_t ofs;
	bool success = false;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	/* NAME의 유효성을 검사한다. */
	if (*name == '\0' || strlen (name) > NAME_MAX)
		return false;

	/* NAME이 사용 중이 아닌지 확인한다. */
	if (lookup (dir, name, NULL, NULL))
		goto done;

	/* OFS를 비어 있는 슬롯의 offset으로 설정한다.
	 * 비어 있는 슬롯이 없으면 현재 end-of-file로 설정된다.
	 *
	 * inode_read_at()은 end of file에서만 short read를 반환한다.
	 * 그렇지 않으면 low memory 같은 일시적인 이유로 short read가 아니었는지
	 * 확인해야 한다. */
	for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
			ofs += sizeof e)
		if (!e.in_use)
			break;

	/* 슬롯에 기록한다. */
	e.in_use = true;
	strlcpy (e.name, name, sizeof e.name);
	e.inode_sector = inode_sector;
	success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

done:
	return success;
}

/* DIR에서 NAME에 대한 엔트리를 제거한다.
 * 성공하면 true를, 실패하면 false를 반환한다.
 * 이 실패는 주어진 NAME을 가진 파일이 없을 때만 발생한다. */
bool
dir_remove (struct dir *dir, const char *name) {
	struct dir_entry e;
	struct inode *inode = NULL;
	bool success = false;
	off_t ofs;

	ASSERT (dir != NULL);
	ASSERT (name != NULL);

	/* 디렉터리 엔트리를 찾는다. */
	if (!lookup (dir, name, &e, &ofs))
		goto done;

	/* inode를 연다. */
	inode = inode_open (e.inode_sector);
	if (inode == NULL)
		goto done;

	/* 디렉터리 엔트리를 지운다. */
	e.in_use = false;
	if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e)
		goto done;

	/* inode를 제거한다. */
	inode_remove (inode);
	success = true;

done:
	inode_close (inode);
	return success;
}

/* DIR에서 다음 디렉터리 엔트리를 읽어 NAME에 이름을 저장한다.
 * 성공하면 true를, 디렉터리에 더 이상 엔트리가 없으면 false를 반환한다. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1]) {
	struct dir_entry e;

	while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) {
		dir->pos += sizeof e;
		if (e.in_use) {
			strlcpy (name, e.name, NAME_MAX + 1);
			return true;
		}
	}
	return false;
}
