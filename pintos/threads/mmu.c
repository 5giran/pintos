#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "threads/init.h"
#include "threads/pte.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "intrinsic.h"

static uint64_t *
pgdir_walk (uint64_t *pdp, const uint64_t va, int create) {
	int idx = PDX (va);
	if (pdp) {
		uint64_t *pte = (uint64_t *) pdp[idx];
		if (!((uint64_t) pte & PTE_P)) {
			if (create) {
				uint64_t *new_page = palloc_get_page (PAL_ZERO);
				if (new_page)
					pdp[idx] = vtop (new_page) | PTE_U | PTE_W | PTE_P;
				else
					return NULL;
			} else
				return NULL;
		}
		return (uint64_t *) ptov (PTE_ADDR (pdp[idx]) + 8 * PTX (va));
	}
	return NULL;
}

static uint64_t *
pdpe_walk (uint64_t *pdpe, const uint64_t va, int create) {
	uint64_t *pte = NULL;
	int idx = PDPE (va);
	int allocated = 0;
	if (pdpe) {
		uint64_t *pde = (uint64_t *) pdpe[idx];
		if (!((uint64_t) pde & PTE_P)) {
			if (create) {
				uint64_t *new_page = palloc_get_page (PAL_ZERO);
				if (new_page) {
					pdpe[idx] = vtop (new_page) | PTE_U | PTE_W | PTE_P;
					allocated = 1;
				} else
					return NULL;
			} else
				return NULL;
		}
		pte = pgdir_walk (ptov (PTE_ADDR (pdpe[idx])), va, create);
	}
	if (pte == NULL && allocated) {
		palloc_free_page ((void *) ptov (PTE_ADDR (pdpe[idx])));
		pdpe[idx] = 0;
	}
	return pte;
}

/* page map level 4 pml4에서 virtual address VADDR에 대한
 * page table entry의 주소를 반환한다.
 * PML4E에 VADDR용 page table이 없으면 동작은 CREATE에 따라 달라진다.
 * CREATE가 true면 새 page table을 만들고 그 안의 pointer를 반환한다.
 * 그렇지 않으면 null pointer를 반환한다. */
uint64_t *
pml4e_walk (uint64_t *pml4e, const uint64_t va, int create) {
	uint64_t *pte = NULL;
	int idx = PML4 (va);
	int allocated = 0;
	if (pml4e) {
		uint64_t *pdpe = (uint64_t *) pml4e[idx];
		if (!((uint64_t) pdpe & PTE_P)) {
			if (create) {
				uint64_t *new_page = palloc_get_page (PAL_ZERO);
				if (new_page) {
					pml4e[idx] = vtop (new_page) | PTE_U | PTE_W | PTE_P;
					allocated = 1;
				} else
					return NULL;
			} else
				return NULL;
		}
		pte = pdpe_walk (ptov (PTE_ADDR (pml4e[idx])), va, create);
	}
	if (pte == NULL && allocated) {
		palloc_free_page ((void *) ptov (PTE_ADDR (pml4e[idx])));
		pml4e[idx] = 0;
	}
	return pte;
}

/* kernel virtual address에 대한 매핑은 있지만 user virtual address에 대한
 * 매핑은 없는 새 page map level 4(pml4)를 만든다.
 * 새 page directory를 반환하고, 메모리 할당에 실패하면 null pointer를 반환한다. */
uint64_t *
pml4_create (void) {
	uint64_t *pml4 = palloc_get_page (0);
	if (pml4)
		memcpy (pml4, base_pml4, PGSIZE);
	return pml4;
}

static bool
pt_for_each (uint64_t *pt, pte_for_each_func *func, void *aux,
		unsigned pml4_index, unsigned pdp_index, unsigned pdx_index) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {
		uint64_t *pte = &pt[i];
		if (((uint64_t) *pte) & PTE_P) {
			void *va = (void *) (((uint64_t) pml4_index << PML4SHIFT) |
								 ((uint64_t) pdp_index << PDPESHIFT) |
								 ((uint64_t) pdx_index << PDXSHIFT) |
								 ((uint64_t) i << PTXSHIFT));
			if (!func (pte, va, aux))
				return false;
		}
	}
	return true;
}

static bool
pgdir_for_each (uint64_t *pdp, pte_for_each_func *func, void *aux,
		unsigned pml4_index, unsigned pdp_index) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {
		uint64_t *pte = ptov((uint64_t *) pdp[i]);
		if (((uint64_t) pte) & PTE_P)
			if (!pt_for_each ((uint64_t *) PTE_ADDR (pte), func, aux,
					pml4_index, pdp_index, i))
				return false;
	}
	return true;
}

static bool
pdp_for_each (uint64_t *pdp,
		pte_for_each_func *func, void *aux, unsigned pml4_index) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {
		uint64_t *pde = ptov((uint64_t *) pdp[i]);
		if (((uint64_t) pde) & PTE_P)
			if (!pgdir_for_each ((uint64_t *) PTE_ADDR (pde), func,
					 aux, pml4_index, i))
				return false;
	}
	return true;
}

/* kernel 영역을 포함한 사용 가능한 각 pte entry에 FUNC를 적용한다. */
bool
pml4_for_each (uint64_t *pml4, pte_for_each_func *func, void *aux) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {
		uint64_t *pdpe = ptov((uint64_t *) pml4[i]);
		if (((uint64_t) pdpe) & PTE_P)
			if (!pdp_for_each ((uint64_t *) PTE_ADDR (pdpe), func, aux, i))
				return false;
	}
	return true;
}

static void
pt_destroy (uint64_t *pt) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {
		uint64_t *pte = ptov((uint64_t *) pt[i]);
		if (((uint64_t) pte) & PTE_P)
			palloc_free_page ((void *) PTE_ADDR (pte));
	}
	palloc_free_page ((void *) pt);
}

static void
pgdir_destroy (uint64_t *pdp) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {
		uint64_t *pte = ptov((uint64_t *) pdp[i]);
		if (((uint64_t) pte) & PTE_P)
			pt_destroy (PTE_ADDR (pte));
	}
	palloc_free_page ((void *) pdp);
}

static void
pdpe_destroy (uint64_t *pdpe) {
	for (unsigned i = 0; i < PGSIZE / sizeof(uint64_t *); i++) {
		uint64_t *pde = ptov((uint64_t *) pdpe[i]);
		if (((uint64_t) pde) & PTE_P)
			pgdir_destroy ((void *) PTE_ADDR (pde));
	}
	palloc_free_page ((void *) pdpe);
}

/* pml4e를 파기하고, 그것이 참조하는 모든 page를 해제한다. */
void
pml4_destroy (uint64_t *pml4) {
	if (pml4 == NULL)
		return;
	ASSERT (pml4 != base_pml4);

	/* PML4(vaddr) >= 1이면 정의상 kernel space다. */
	uint64_t *pdpe = ptov ((uint64_t *) pml4[0]);
	if (((uint64_t) pdpe) & PTE_P)
		pdpe_destroy ((void *) PTE_ADDR (pdpe));
	palloc_free_page ((void *) pml4);
}

/* page directory PD를 CPU의 page directory base register에 적재한다. */
void
pml4_activate (uint64_t *pml4) {
	lcr3 (vtop (pml4 ? pml4 : base_pml4));
}

/* pml4에서 user virtual address UADDR에 대응하는 physical address를 찾는다.
 * 그 physical address에 대응하는 kernel virtual address를 반환하며,
 * UADDR이 매핑되어 있지 않으면 null pointer를 반환한다. */
void *
pml4_get_page (uint64_t *pml4, const void *uaddr) {
	ASSERT (is_user_vaddr (uaddr));

	uint64_t *pte = pml4e_walk (pml4, (uint64_t) uaddr, 0);

	if (pte && (*pte & PTE_P))
		return ptov (PTE_ADDR (*pte)) + pg_ofs (uaddr);
	return NULL;
}

/* page map level 4 PML4에 user virtual page UPAGE에서
 * kernel virtual address KPAGE가 가리키는 physical frame으로의 매핑을 추가한다.
 * UPAGE는 이미 매핑되어 있으면 안 된다. KPAGE는 보통
 * palloc_get_page()로 user pool에서 얻은 page여야 한다.
 * WRITABLE이 true면 새 page는 read/write이고,
 * 그렇지 않으면 read-only다.
 * 성공하면 true, 메모리 할당 실패 시 false를 반환한다. */
bool
pml4_set_page (uint64_t *pml4, void *upage, void *kpage, bool rw) {
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (pg_ofs (kpage) == 0);
	ASSERT (is_user_vaddr (upage));
	ASSERT (pml4 != base_pml4);

	uint64_t *pte = pml4e_walk (pml4, (uint64_t) upage, 1);

	if (pte)
		*pte = vtop (kpage) | PTE_P | (rw ? PTE_W : 0) | PTE_U;
	return pte != NULL;
}

/* page directory PD에서 user virtual page UPAGE를 "not present"로 표시한다.
 * 이후 이 page에 접근하면 fault가 발생한다. page table entry의 다른 bit는
 * 유지된다.
 * UPAGE는 매핑되어 있지 않아도 된다. */
void
pml4_clear_page (uint64_t *pml4, void *upage) {
	uint64_t *pte;
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (is_user_vaddr (upage));

	pte = pml4e_walk (pml4, (uint64_t) upage, false);

	if (pte != NULL && (*pte & PTE_P) != 0) {
		*pte &= ~PTE_P;
		if (rcr3 () == vtop (pml4))
			invlpg ((uint64_t) upage);
	}
}

/* PML4에서 virtual page VPAGE에 대한 PTE가 dirty이면 true를 반환한다.
 * 즉, PTE가 설치된 뒤 그 page가 수정되었다는 뜻이다.
 * PML4에 VPAGE용 PTE가 없으면 false를 반환한다. */
bool
pml4_is_dirty (uint64_t *pml4, const void *vpage) {
	uint64_t *pte = pml4e_walk (pml4, (uint64_t) vpage, false);
	return pte != NULL && (*pte & PTE_D) != 0;
}

/* PML4에서 virtual page VPAGE의 PTE에 있는 dirty bit를 DIRTY로 설정한다. */
void
pml4_set_dirty (uint64_t *pml4, const void *vpage, bool dirty) {
	uint64_t *pte = pml4e_walk (pml4, (uint64_t) vpage, false);
	if (pte) {
		if (dirty)
			*pte |= PTE_D;
		else
			*pte &= ~(uint32_t) PTE_D;

		if (rcr3 () == vtop (pml4))
			invlpg ((uint64_t) vpage);
	}
}

/* PML4에서 virtual page VPAGE의 PTE가 최근에 accessed되었으면 true를 반환한다.
 * 즉, PTE가 설치된 시점부터 마지막으로 clear된 시점 사이에 접근된 경우다.
 * PML4에 VPAGE용 PTE가 없으면 false를 반환한다. */
bool
pml4_is_accessed (uint64_t *pml4, const void *vpage) {
	uint64_t *pte = pml4e_walk (pml4, (uint64_t) vpage, false);
	return pte != NULL && (*pte & PTE_A) != 0;
}

/* PD에서 virtual page VPAGE의 PTE에 있는 accessed bit를 ACCESSED로 설정한다. */
void
pml4_set_accessed (uint64_t *pml4, const void *vpage, bool accessed) {
	uint64_t *pte = pml4e_walk (pml4, (uint64_t) vpage, false);
	if (pte) {
		if (accessed)
			*pte |= PTE_A;
		else
			*pte &= ~(uint32_t) PTE_A;

		if (rcr3 () == vtop (pml4))
			invlpg ((uint64_t) vpage);
	}
}
