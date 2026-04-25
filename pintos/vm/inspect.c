/* inspect.c: VM을 위한 테스트 유틸리티. */
/* 이 파일을 수정하지 마십시오. */

#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "vm/inspect.h"

static void
inspect (struct intr_frame *f) {
	const void *va = (const void *) f->R.rax;
	f->R.rax = PTE_ADDR (pml4_get_page (thread_current ()->pml4, va));
}

/* VM 컴포넌트를 테스트하는 도구. 이 함수는 int 0x42를 통해 호출한다.
 * 입력:
 * @RAX - 검사할 가상 주소
 * 출력:
 * @RAX - 입력에 mmap된 물리 주소. */
void
register_inspect_intr (void) {
	intr_register_int (0x42, 3, INTR_OFF, inspect, "Inspect Virtual Memory");
}
