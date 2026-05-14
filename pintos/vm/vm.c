/* vm.c: virtual memory objects의 일반 인터페이스. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include <string.h>

/* page의 va 값을 key로 삼아 hash table bucket 선택용 해시값을 만든다. */
static uint64_t
hash_func (const struct hash_elem *e, void* aux) {
	struct page* page = hash_entry (e, struct page, hash_elem);
	void *va = pg_round_down(page->va);

	return hash_bytes (&va, sizeof va);
}

/* 두 page의 key인 va 값을 비교한다. */
static bool
less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux) {
	struct page* pa = hash_entry (a, struct page, hash_elem);
	struct page* pb = hash_entry (b, struct page, hash_elem);

	return pg_round_down(pa->va) < pg_round_down(pb->va);
}


/* 각 subsystem의 초기화 코드를 호출하여 virtual memory subsystem을 초기화한다. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* project 4용 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* 위 줄을 수정하지 마십시오. */
	/* TODO: 여기에 코드를 작성하라. */
}

/* 페이지의 type을 가져온다. 이 함수는 페이지가 초기화된 뒤의 type을 알고 싶을 때 유용하다.
 * 이 함수는 현재 완전히 구현되어 있다. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* 헬퍼 */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* initializer가 있는 pending page object를 생성한다. 페이지를 만들고 싶다면 직접 생성하지 말고 이 함수나
 * `vm_alloc_page`를 통해 생성하라. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;



	/* upage가 이미 점유되어 있는지 확인한다. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: 페이지를 생성하고, VM type에 따라 initializer를 가져온 다음,
		 * TODO: uninit_new를 호출하여 "uninit" page struct를 생성한다. 그 후
		 * TODO: uninit_new를 호출한 뒤 필드를 수정해야 한다. */
		bool (*initializer)(struct page *, enum vm_type, void *) = NULL;
		switch (type & 0b11)
		{
		case VM_ANON:
			initializer = anon_initializer;
			break;
		case VM_FILE:
			initializer = file_backed_initializer;
			break;
		
		default:
			break;
		}
		struct page *page = malloc (sizeof (struct page));
		if (page == NULL) {
			goto err;
		}
		uninit_new (page, upage, init, type, aux, initializer);
		page->writable = writable;
		
		if (!spt_insert_page (spt, page)) {
			printf ("페이지를 SPT에 넣는데 실패했어요....===\n");

			free(page);
			goto err;
		}
		return true;
	}
err:
printf ("아마 spt가 제대로 kill 되지 않은 문제일 수 있어요. ...===\n");
	return false;
}

/* spt에서 VA를 찾아 page를 반환한다. 오류 시 NULL을 반환한다. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page dummy;
	dummy.va = va;
	/* TODO: 이 함수를 채워라. */
	struct hash_elem *he = hash_find (&spt->table, &dummy.hash_elem);
	if (he == NULL) { 
		return NULL;
	}
	struct page* page = hash_entry (he, struct page, hash_elem);

	return page;
}

/* PAGE를 spt에 삽입한다. 같은 va가 이미 있으면 false를 반환한다. */

bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page) {
	int succ = false;
	
	/* spt 자체가 아니라 내부 page table을 넘겨야 한다 */
	if (hash_insert (&spt->table, &page->hash_elem) == NULL) {
		succ = true;
	}

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* evict될 struct frame을 가져온다. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: eviction 정책은 당신이 정한다. */

	return victim;
}

/* 페이지 하나를 evict하고 해당 frame을 반환한다.
 * 오류 시 NULL을 반환한다. */
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: victim을 swap out하고 evicted frame을 반환한다. */

	return NULL;
}

/* palloc()으로 frame을 가져온다. 사용 가능한 page가 없으면 page를 evict하고 반환한다. 이 함수는 항상 유효한
 * address를 반환한다. 즉, user pool memory가 가득 차 있으면 이 함수는 사용 가능한 memory space를 얻기
 * 위해 frame을 evict한다. */
static struct frame *
vm_get_frame (void) {
	struct frame *frame = malloc (sizeof (struct frame));
	ASSERT (frame != NULL);
	frame->kva = palloc_get_page (PAL_USER | PAL_ZERO);
	// TODO. 실패하면 쫒아낼 victim을 골라서 걔를 쫒아낸 뒤, 그 공간을 반환하는 로직을 작성해야 합니다.
	ASSERT (frame->kva != NULL);
	frame->page = NULL;
	ASSERT (frame->page == NULL);
	return frame;
}

/* 스택을 확장한다. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* write_protected page에서 발생한 fault를 처리한다. */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* 성공하면 true를 반환한다. */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = NULL;
	if (addr == NULL) {
		return false;
	}
	/* TODO: fault를 검증한다. */
	page = spt_find_page (spt, addr);
	if (page == NULL) {
		// printf ("vm_try_handle_fault에서 찾은 spt entry가 null 이에요.\n");
		return false;
	}
	return vm_do_claim_page (page);
}

/* page를 해제한다.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* VA에 할당된 page를 claim한다. */
bool
vm_claim_page (void *va) {
	struct page *page = NULL;
	/* TODO: 이 함수를 채우세요. */
	struct supplemental_page_table *spt = &thread_current ()->spt;
	page = spt_find_page (spt, va);	
	ASSERT (page != NULL);
	return vm_do_claim_page (page);
}

/* PAGE를 claim하고 mmu를 설정한다. 
	페이지를 claim한다는 것은 물리 프레임(physical frame)을 할당한다는 뜻입니다. 
	먼저 vm_get_frame을 호출하여 프레임을 얻습니다. 
	이 부분은 템플릿에서 이미 처리되어 있습니다. 
	그런 다음 MMU를 설정해야 합니다. 
	다시 말해 페이지 테이블(page table)에 가상 주소에서 물리 주소로의 매핑을 
	추가해야 합니다. 반환값은 이 작업이 성공했는지 여부를 나타내야 합니다.
*/
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* 링크를 설정한다. */
	frame->page = page;
	page->frame = frame;

	/* TODO: page table entry를 삽입하여 page의 VA를 frame의 PA에 매핑한다. */
	struct thread* current = thread_current ();
	ASSERT (current != NULL);
	ASSERT (current->pml4 != NULL);
	if (pml4_get_page (current->pml4, page->va) == NULL)
		pml4_set_page (current->pml4, page->va, frame->kva, page->writable);
	bool result = swap_in (page, frame->kva);

	return result;
	// TODO. 시도 도중 실패했다면, 자원 해제하기
}

/* 새 supplemental page table을 초기화한다. */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	struct hash *hash = &spt->table;
	hash_init (hash, hash_func, less_func, NULL);
}

/* parent page에 매핑된 프레임의 정보를 dst_page에 매핑된 프레임으로 복사한다. */
bool
vm_copy_initializer (struct page *dst_page, void *aux) {  // TODO. 직관적이고, 좋은 이름을 고민해보자....
	// TODO. 촘촘한 예외 처리를 완성하자. 일단은 기본 흐름에 대해서만 진행하자.
	if (dst_page->frame == NULL || dst_page->frame->kva == NULL) {
		return false;
	}
	struct page *src_page = (struct page *) aux;
	if (src_page->frame == NULL || src_page->frame->kva == NULL) {
		return false;
	}

	memcpy (dst_page->frame->kva, src_page->frame->kva, PGSIZE);
	return true;
}

/* supplemental page table을 src에서 dst로 복사한다. */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src) {
	struct hash_iterator i;
	hash_first (&i, src);
	while (hash_next (&i)) {
		struct page *src_page = hash_entry (hash_cur (&i), struct page, hash_elem);
		enum vm_type type = page_get_type (src_page);
		void* va = src_page->va;
		bool writable = src_page->writable;
		vm_initializer *init = NULL;
		void *aux = NULL;
		if (type == VM_UNINIT) {
			init = &src_page->uninit.init;
			aux = &src_page->uninit.aux;
			if (!vm_alloc_page_with_initializer (type, va, writable, init, aux)) {
				return false;
			}
		}
		else if (page_get_type (src_page) == VM_ANON) {
			init = vm_copy_initializer;
			aux = src_page;
			if (!vm_alloc_page_with_initializer (VM_ANON, va, writable, init, aux)) {
				return false;
			}
			if (!vm_claim_page (va)) {
				return false;
			}
		}
	}				
	return true;
}



void
vm_hash_destroy_func (struct hash_elem *e, void *aux UNUSED) {
	struct page * page = hash_entry (e, struct page, hash_elem);

	free (page->frame);
	free (page);
}

/* supplemental page table이 보유한 resource를 해제한다. */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: thread가 보유한 모든 supplemental_page_table을 destroy하고
	 * TODO: 수정된 모든 내용을 storage에 writeback한다. */
	
	
	 // TODO. 지금은 그냥 아예 데이터를 삭제해버려요. 하지만 위 TODO가 필요할 수 있어요.

	// TODO. destroy 함수가 필요해요. hash_elem을 받아서 page 자체를 할당 해제하는 함수가 필요해요.
	hash_clear (&spt->table, vm_hash_destroy_func);
}
