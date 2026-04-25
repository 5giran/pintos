/* page_cache.c: 페이지 캐시(Buffer Cache) 구현. */

#include "vm/vm.h"
static bool page_cache_readahead (struct page *page, void *kva);
static bool page_cache_writeback (struct page *page);
static void page_cache_destroy (struct page *page);

/* 이 struct는 수정하지 마십시오. */
static const struct page_operations page_cache_op = {
	.swap_in = page_cache_readahead,
	.swap_out = page_cache_writeback,
	.destroy = page_cache_destroy,
	.type = VM_PAGE_CACHE,
};

tid_t page_cache_workerd;

/* vm 파일의 초기화 함수 */
void
pagecache_init (void) {
	/* TODO: page_cache_kworkerd를 사용해 page cache용 워커 데몬을 만드십시오. */
}

/* page cache를 초기화합니다. */
bool
page_cache_initializer (struct page *page, enum vm_type type, void *kva) {
	/* handler를 설정합니다. */
	page->operations = &page_cache_op;

}

/* readhead를 구현하기 위해 Swap in 메커니즘을 활용합니다. */
static bool
page_cache_readahead (struct page *page, void *kva) {
}

/* writeback을 구현하기 위해 Swap out 메커니즘을 활용합니다. */
static bool
page_cache_writeback (struct page *page) {
}

/* page_cache를 해제합니다. */
static void
page_cache_destroy (struct page *page) {
}

/* page cache용 worker thread */
static void
page_cache_kworkerd (void *aux) {
}
