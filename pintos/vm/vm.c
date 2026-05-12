/* vm.c: virtual memory objects의 일반 인터페이스. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

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
		switch (type)
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
		uninit_new (page, upage, init, type, aux, initializer);
		
		if (!spt_insert_page (spt, page)) {
			free(page);
			goto err;
		}
		return true;
	}
err:
	return false;
}

/* spt에서 VA를 찾아 page를 반환한다. 오류 시 NULL을 반환한다. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = malloc (sizeof (struct page));
	page->va = va;

	struct hash_elem *he = hash_find (&spt->table, &page->hash_elem);
	free (page);
	
	if (he == NULL) return NULL;
	page = hash_entry (he, struct page, hash_elem);

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
	struct frame *frame = NULL;
	/* TODO: 이 함수를 채우세요. */

	ASSERT (frame != NULL);
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
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: fault를 검증한다. */
	/* TODO: 여기에 코드를 작성하세요. */

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
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: 이 함수를 채우세요. */

	return vm_do_claim_page (page);
}

/* PAGE를 claim하고 mmu를 설정한다. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* 링크를 설정한다. */
	frame->page = page;
	page->frame = frame;

	/* TODO: page table entry를 삽입하여 page의 VA를 frame의 PA에 매핑한다. */

	return swap_in (page, frame->kva);
}

/* 새 supplemental page table을 초기화한다. */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	struct hash *hash = &spt->table;
	hash_init (hash, hash_func, less_func, NULL);
}

/* supplemental page table을 src에서 dst로 복사한다. */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* supplemental page table이 보유한 resource를 해제한다. */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: thread가 보유한 모든 supplemental_page_table을 destroy하고
	 * TODO: 수정된 모든 내용을 storage에 writeback한다. */
}
