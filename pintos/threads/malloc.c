#include "threads/malloc.h"
#include <debug.h>
#include <list.h>
#include <round.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* malloc()의 간단한 구현.
   The size of each request, in bytes, is rounded up to a power
   of 2 and assigned to the "descriptor" that manages blocks of
   that size.  The descriptor keeps a list of free blocks.  If
   the free list is nonempty, one of its blocks is used to
   satisfy the request.
   Otherwise, a new page of memory, called an "arena", is
   obtained from the page allocator (if none is available,
   malloc() returns a null pointer).  The new arena is divided
   into blocks, all of which are added to the descriptor's free
   list.  Then we return one of the new blocks.
   When we free a block, we add it to its descriptor's free list.
   But if the arena that the block was in now has no in-use
   blocks, we remove all of the arena's blocks from the free list
   and give the arena back to the page allocator.
   We can't handle blocks bigger than 2 kB using this scheme,
   because they're too big to fit in a single page with a
   descriptor.  We handle those by allocating contiguous pages
   with the page allocator and sticking the allocation size at
   the beginning of the allocated block's arena header. */

/* descriptor(디스크립터). */
struct desc {
	size_t block_size;          /* 각 원소의 크기(바이트). */
	size_t blocks_per_arena;    /* arena 하나의 block 수. */
	struct list free_list;      /* free block 목록. */
	struct lock lock;           /* lock(락). */
};

/* arena 손상을 감지하기 위한 magic number. */
#define ARENA_MAGIC 0x9a548eed

/* arena(아레나). */
struct arena {
	unsigned magic;             /* 항상 ARENA_MAGIC으로 설정된다. */
	struct desc *desc;          /* 소유 descriptor, 큰 block이면 null. */
	size_t free_cnt;            /* free block 수, 큰 block이면 page 수. */
};

/* free block(빈 블록). */
struct block {
	struct list_elem free_elem; /* free list 원소. */
};

/* descriptor 집합. */
static struct desc descs[10];   /* descriptor들. */
static size_t desc_cnt;         /* descriptor 개수. */

static struct arena *block_to_arena (struct block *);
static struct block *arena_to_block (struct arena *, size_t idx);

/* malloc() descriptor들을 초기화한다. */
void
malloc_init (void) {
	size_t block_size;

	for (block_size = 16; block_size < PGSIZE / 2; block_size *= 2) {
		struct desc *d = &descs[desc_cnt++];
		ASSERT (desc_cnt <= sizeof descs / sizeof *descs);
		d->block_size = block_size;
		d->blocks_per_arena = (PGSIZE - sizeof (struct arena)) / block_size;
		list_init (&d->free_list);
		lock_init (&d->lock);
	}
}

/* 최소 SIZE 바이트 이상의 새 block을 얻어 반환한다.
   메모리를 사용할 수 없으면 null pointer를 반환한다. */
void *
malloc (size_t size) {
	struct desc *d;
	struct block *b;
	struct arena *a;

	/* 0바이트 요청에는 null pointer면 충분하다. */
	if (size == 0)
		return NULL;

	/* SIZE바이트 요청을 만족하는 가장 작은 descriptor를 찾는다. */
	for (d = descs; d < descs + desc_cnt; d++)
		if (d->block_size >= size)
			break;
	if (d == descs + desc_cnt) {
		/* SIZE가 어떤 descriptor로도 처리하기에는 너무 크다.
		   SIZE와 arena를 담을 만큼 충분한 page를 할당한다. */
		size_t page_cnt = DIV_ROUND_UP (size + sizeof *a, PGSIZE);
		a = palloc_get_multiple (0, page_cnt);
		if (a == NULL)
			return NULL;

		/* PAGE_CNT page로 이루어진 큰 block임을 나타내도록 arena를 초기화하고
		   반환한다. */
		a->magic = ARENA_MAGIC;
		a->desc = NULL;
		a->free_cnt = page_cnt;
		return a + 1;
	}

	lock_acquire (&d->lock);

	/* free list가 비어 있으면 새 arena를 만든다. */
	if (list_empty (&d->free_list)) {
		size_t i;

		/* page를 할당한다. */
		a = palloc_get_page (0);
		if (a == NULL) {
			lock_release (&d->lock);
			return NULL;
		}

		/* arena를 초기화하고 그 block들을 free list에 추가한다. */
		a->magic = ARENA_MAGIC;
		a->desc = d;
		a->free_cnt = d->blocks_per_arena;
		for (i = 0; i < d->blocks_per_arena; i++) {
			struct block *b = arena_to_block (a, i);
			list_push_back (&d->free_list, &b->free_elem);
		}
	}

	/* free list에서 block 하나를 가져와 반환한다. */
	b = list_entry (list_pop_front (&d->free_list), struct block, free_elem);
	a = block_to_arena (b);
	a->free_cnt--;
	lock_release (&d->lock);
	return b;
}

/* A * B 바이트를 할당하고 0으로 초기화해 반환한다.
   메모리를 사용할 수 없으면 null pointer를 반환한다. */
void *
calloc (size_t a, size_t b) {
	void *p;
	size_t size;

	/* block 크기를 계산하고 size_t에 들어가는지 확인한다. */
	size = a * b;
	if (size < a || size < b)
		return NULL;

	/* 메모리를 할당하고 0으로 채운다. */
	p = malloc (size);
	if (p != NULL)
		memset (p, 0, size);

	return p;
}

/* BLOCK에 할당된 바이트 수를 반환한다. */
static size_t
block_size (void *block) {
	struct block *b = block;
	struct arena *a = block_to_arena (b);
	struct desc *d = a->desc;

	return d != NULL ? d->block_size : PGSIZE * a->free_cnt - pg_ofs (block);
}

/* OLD_BLOCK의 크기를 NEW_SIZE 바이트로 조정하려고 시도하며,
   그 과정에서 위치가 바뀔 수 있다.
   성공하면 새 block을, 실패하면 null pointer를 반환한다.
   OLD_BLOCK이 null인 호출은 malloc(NEW_SIZE)와 같다.
   NEW_SIZE가 0인 호출은 free(OLD_BLOCK)와 같다. */
void *
realloc (void *old_block, size_t new_size) {
	if (new_size == 0) {
		free (old_block);
		return NULL;
	} else {
		void *new_block = malloc (new_size);
		if (old_block != NULL && new_block != NULL) {
			size_t old_size = block_size (old_block);
			size_t min_size = new_size < old_size ? new_size : old_size;
			memcpy (new_block, old_block, min_size);
			free (old_block);
		}
		return new_block;
	}
}

/* block P를 해제한다. 이 block은 이전에 malloc(), calloc(), 또는
   realloc()으로 할당된 것이어야 한다. */
void
free (void *p) {
	if (p != NULL) {
		struct block *b = p;
		struct arena *a = block_to_arena (b);
		struct desc *d = a->desc;

		if (d != NULL) {
			/* 일반 block이다. 여기서 처리한다. */

#ifndef NDEBUG
			/* use-after-free 버그를 감지하기 쉽도록 block을 지운다. */
			memset (b, 0xcc, d->block_size);
#endif

			lock_acquire (&d->lock);

			/* block을 free list에 추가한다. */
			list_push_front (&d->free_list, &b->free_elem);

			/* arena가 이제 완전히 비어 있으면 해제한다. */
			if (++a->free_cnt >= d->blocks_per_arena) {
				size_t i;

				ASSERT (a->free_cnt == d->blocks_per_arena);
				for (i = 0; i < d->blocks_per_arena; i++) {
					struct block *b = arena_to_block (a, i);
					list_remove (&b->free_elem);
				}
				palloc_free_page (a);
			}

			lock_release (&d->lock);
		} else {
			/* 큰 block이다. 해당 page들을 해제한다. */
			palloc_free_multiple (a, a->free_cnt);
			return;
		}
	}
}

/* block B가 속한 arena를 반환한다. */
static struct arena *
block_to_arena (struct block *b) {
	struct arena *a = pg_round_down (b);

	/* arena가 유효한지 확인한다. */
	ASSERT (a != NULL);
	ASSERT (a->magic == ARENA_MAGIC);

	/* block이 arena 기준으로 올바르게 정렬되어 있는지 확인한다. */
	ASSERT (a->desc == NULL
			|| (pg_ofs (b) - sizeof *a) % a->desc->block_size == 0);
	ASSERT (a->desc != NULL || pg_ofs (b) == sizeof *a);

	return a;
}

/* arena A 안의 (IDX - 1)번째 block을 반환한다. */
static struct block *
arena_to_block (struct arena *a, size_t idx) {
	ASSERT (a != NULL);
	ASSERT (a->magic == ARENA_MAGIC);
	ASSERT (idx < a->desc->blocks_per_arena);
	return (struct block *) ((uint8_t *) a
			+ sizeof *a
			+ idx * a->desc->block_size);
}
