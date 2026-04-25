/* file.c: memory backed file object(mmaped object)의 구현. */

#include "vm/vm.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* 이 struct를 수정하지 마십시오 */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* file vm의 초기화자 */
void
vm_file_init (void) {
}

/* file-backed page를 초기화한다 */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* handler를 설정한다 */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* 파일에서 내용을 읽어 페이지를 swap in 한다. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* 내용을 파일로 writeback하여 페이지를 swap out 한다. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* file-backed page를 파괴한다. PAGE는 호출자가 해제한다. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* mmap을 수행한다 */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
}

/* munmap을 수행한다 */
void
do_munmap (void *addr) {
}
