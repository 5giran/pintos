#include "userprog/uaccess.h"
#include <string.h>
#include <stdbool.h>
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"


bool
validate_user_read(const void *buffer, size_t size)
{
    if (size == 0) return true;
    if (buffer == NULL) return false; // size > 0인데 주소가 없다, 메모리 접근 불가. 바로 false

	// 반복문 돌리기 위해서 주소를 1바이트씩 이동시킬 수 있게 변경: buffer의 시작 바이트 주소
    const uint8_t *bf =(const uint8_t *)buffer;
	// end address 따로 정의: buffer의 끝 바이트 주소
	const uint8_t *end_adr = bf+size-1;

	// 끝 주소 = 시작 주소보다 같거나 커야 한다, 근데 시작 주소보다 작다 (초과분이 잘린거임)
	if (end_adr < bf) return false; // 사이즈가 말도 안되게 큰 경우, 주소 오버플로우 처리

	// 순회할 시작페이지, 끝 페이지 정의
	const uint8_t *start_page = pg_round_down(bf);
	const uint8_t *end_page = pg_round_down(end_adr);

    // buffer가 걸쳐진 모든 page를 순회 (페이지 단위로 확인)
    // buffer의 시작주소 + PGSIZE: buffer의 끝 주소까지 순회
    for (const uint8_t *i = start_page; i <= end_page; i+=PGSIZE) {
		// page 시작주소가 user virtual address 범위 안에 있지 않다면 false
		if (!is_user_vaddr(i)) return false;
		// page가 실제 mapped 되어있지 않다면 false
		// pml4_get_page 함수 인수 확인
		if (!pml4_get_page(thread_current()->pml4, i)) return false;
    }

	// 여기까지 왔다는건 성공했다는 것
	return true;
}

bool
validate_user_write(const void *buffer, size_t size)
{

}



void
copy_in (void *dst, const void *usrc, size_t size)
{
	/* TODO: user memory -> kernel memory */
}

void
copy_out (void *udst, const void *src, size_t size)
{
	/* TODO: kernel memory -> user memory */
}

char *
copy_in_string (const char *ustr)
{
	/* TODO: user string을 kernel buffer로 복사 */
	return NULL;
}