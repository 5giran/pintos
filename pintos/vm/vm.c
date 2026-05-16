/* vm.c: virtual memory objects의 일반 인터페이스. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include <string.h>
#include "debug_log.h"

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
spt_find_page (struct supplemental_page_table *spt, void *va) {
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
vm_stack_growth (void *addr) {
	vm_alloc_page (VM_ANON, addr, true);
	vm_claim_page (addr);
}

/* write_protected page에서 발생한 fault를 처리한다. */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

bool
is_valid_stack_growth_request (bool user, struct intr_frame* f, void* addr, struct page* page) {
	/*	stack growth 를 위한 요청이라면...
		1. spt 에 없어야 함.
		2. 스택이라 주장하는 지점이 stack start point 에서 1MB 보다 더 멀리 떨어져서는 안 됨.
		3. rsp - 8보다 작아서는 안 됨.
	*/
	uintptr_t rsp;
	if (user) {
		rsp = f->rsp;
	} else {
		rsp = thread_current ()->rsp;
		if (rsp == NULL) {
			DBG ("kernel page fault 발생, 정당하지 않은 메모리 접근 시도를 차단합니다.\n");
			return false;
		}
	}

	if (!is_user_vaddr (addr)) {
		printf ("유저 가상 메모리가 아님... 죽었다	\n");
		return false;
	}

	if (
		(page == NULL) 
		&& (addr >= rsp - 8) 
		&& ((USER_STACK - (uintptr_t) pg_round_down (addr)) <= (1 << 20))
	) {
		return true;
	}

	DBG ("is_valid_stack_growth_request: false\n");
	return false;
}

/* 성공하면 true를 반환한다. */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	if (!not_present) {
		DBG ("not present, DIE...	\n");
		return false;
	}
	if (addr == NULL) {
		DBG ("addr NULL... DIE...	\n");
		return false;
	}

	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = NULL;
	void * page_addr = pg_round_down (addr);

	page = spt_find_page (spt, page_addr);
	if (is_valid_stack_growth_request (user, f, addr, page)) {
		vm_stack_growth (page_addr);
		return true;
	} 
	/* TODO. else 를 쓰지 않고 이렇게 하는 이유: vm_stack_growth의 반환값이 static void 타입으로 고정되어 있다. 
	따라서 실제 페이지가 만들어졌는지를 한 번 더 체크해줘야 한다.
	하지만 이 예외 체크도 완벽히 모든 예외를 체크한다고 할 수는 없다... (page는 생성되었는데 물리 프레임과 매핑이 실패했다면??)
	*/
	page = spt_find_page (spt, page_addr);
	if (page == NULL) {  
		DBG ("정당하지 않은 page fault 이에요.\n");
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
	ASSERT (page->frame == NULL);
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
		DBG ("vm_copy_initializer: dst 페이지가 아무런 프레임과 매핑되어 있지 않아요...\n");
		return false;
	}
	struct page *src_page = (struct page *) aux;
	if (src_page->frame == NULL || src_page->frame->kva == NULL) {
		DBG ("aux:			%p\n", aux);
		DBG ("src_page:		%p\n", src_page);
		DBG ("type:			%d\n", src_page->operations->type);
		DBG ("vm_copy_initializer: src 페이지가 아무런 프레임과 매핑되어 있지 않아요...\n");
		DBG ("vm_copy_initializer: page_frame:%p, type: %d\n", src_page->frame, src_page->operations->type);
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
		enum vm_type type = src_page->operations->type;
		void* va = src_page->va;
		bool writable = src_page->writable;
		vm_initializer *init = NULL;
		void *aux = NULL;
		if (type == VM_UNINIT) {
			init = src_page->uninit.init;
			DBG ("VM_UNINIT 일 때 고정된 쓰레기 값이 오나요??, init 포인터 주소 %p\n",  init);
			aux = src_page->uninit.aux;
			if (!vm_alloc_page_with_initializer (page_get_type (src_page), va, writable, init, aux)) {
				DBG ("uninit page type 생성 실패...\n");
				return false;
			}
		}
		else if (type == VM_ANON) {
			init = vm_copy_initializer;
			aux = src_page;
			DBG ("VM_ANON 일 때 고정된 쓰레기 값이 오나요??, init 포인터 주소 %p\n",  init);
			if (!vm_alloc_page_with_initializer (VM_ANON, va, writable, init, aux)) {
				DBG ("anon page type 생성 실패...\n");
				return false;
			}
			if (!vm_claim_page (va)) {
				DBG ("anon page에 매핑할 프레임 생성 실패/연결 실패...\n");
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
