# Pintos Project3 VM 제공 헬퍼 API 정리

> 기준: 현재 프로젝트 코드(`/Users/bruce/Documents/New project 3/pintos`)와 로컬 VM 정리 문서.
> 목적: VM 과제를 구현할 때 이미 제공된 헬퍼 API를 빠르게 찾아 쓰기 위한 실무용 메모.

## 1. 주소와 페이지 정렬

관련 파일: `include/threads/vaddr.h`, `include/threads/pte.h`

### `PGSIZE`, `PGMASK`

- `PGSIZE`: 한 페이지 크기. Pintos에서는 4096 bytes.
- `PGMASK`: 페이지 내부 offset bit를 뽑거나 제거할 때 쓰는 mask.

사용처:

- lazy loading에서 한 페이지 단위로 `read_bytes`, `zero_bytes`를 나눌 때.
- mmap 주소와 offset이 page-aligned인지 검사할 때.
- stack bottom을 `USER_STACK - PGSIZE`로 잡을 때.

### `pg_ofs(va)`

주소가 페이지 안에서 몇 byte offset인지 반환한다.

사용처:

- `pg_ofs(addr) == 0`이면 page-aligned 주소다.
- `mmap`의 `addr`, `offset` 검증에서 자주 쓴다.
- `pml4_set_page()`는 `upage`, `kpage` 모두 page-aligned여야 한다.

### `pg_round_down(va)`

주소를 해당 페이지의 시작 주소로 내린다.

사용처:

- SPT lookup key를 만들 때 거의 필수.
- page fault 주소 `fault_addr`는 페이지 중간일 수 있으므로, `spt_find_page()` 내부에서 내리는 방식이 안전하다.
- stack growth에서 새로 만들 page 주소를 정할 때.

### `pg_round_up(va)`

주소를 다음 페이지 경계로 올린다.

사용처:

- mmap 길이를 페이지 단위로 맞출 때.
- 파일 segment의 총 메모리 범위를 page 단위로 계산할 때.

### `pg_next(va)`

현재 주소가 속한 페이지의 다음 페이지 시작 주소를 반환한다.

사용처:

- 사용자 buffer/string 검증에서 페이지 단위 순회.
- 현재 `userprog/syscall.c`의 `get_next_page_if_valid()`가 사용 중이다.

### `is_user_vaddr(vaddr)`, `is_kernel_vaddr(vaddr)`

주소가 사용자 영역인지, 커널 영역인지 검사한다.

사용처:

- page fault 주소 검증.
- syscall 인자로 들어온 사용자 포인터 검증.
- mmap 범위가 user virtual address 안에 있는지 확인.

주의:

- `is_user_vaddr()`는 주소가 사용자 범위인지 보는 것이지, 실제 매핑되어 있다는 뜻은 아니다.
- 실제 매핑 여부는 `pml4_get_page()` 또는 SPT 조회로 따로 확인해야 한다.

### `USER_STACK`

사용자 스택 최상단 주소.

사용처:

- 초기 stack page: `USER_STACK - PGSIZE`.
- `setup_stack()`에서 `if_->rsp = USER_STACK`로 시작점을 잡는다.
- stack growth 최대 범위 판단 기준으로도 쓴다.

## 2. 페이지 테이블, PML4

관련 파일: `include/threads/mmu.h`, `threads/mmu.c`

### `pml4_create()`

새 프로세스용 page table을 만든다.

사용처:

- `load()`에서 새 실행 파일을 올릴 때.
- `fork()`에서 자식 프로세스의 page table을 만들 때.

### `pml4_activate(pml4)`

현재 CPU가 사용할 page table을 바꾼다.

사용처:

- `process_activate()`에서 스레드 전환 시 호출된다.
- 새 page table을 만든 뒤 사용자 주소 공간을 다루기 전에 활성화해야 한다.

### `pml4_destroy(pml4)`

page table과 그 하위 page table 구조를 해제한다.

사용처:

- `process_cleanup()`에서 프로세스 종료 시 호출된다.

주의:

- VM 구현에서는 실제 user frame 해제와 SPT 정리는 `supplemental_page_table_kill()`에서 먼저 처리하는 흐름이 자연스럽다.

### `pml4_get_page(pml4, uaddr)`

사용자 가상 주소가 현재 어떤 kernel virtual address로 매핑되어 있는지 반환한다.
매핑이 없으면 `NULL`.

사용처:

- syscall 사용자 포인터가 이미 매핑되어 있는지 확인.
- 기존 즉시 로딩 방식의 `install_page()`에서 중복 매핑 검사.
- 디버깅 중 특정 user address가 실제 frame에 올라왔는지 확인.

주의:

- 인자로 user virtual address만 넣어야 한다.
- lazy page는 아직 page table에 없을 수 있으므로, VM 과제에서는 SPT 조회와 함께 봐야 한다.

### `pml4_set_page(pml4, upage, kpage, writable)`

사용자 가상 페이지 `upage`를 물리 frame의 kernel virtual address `kpage`에 매핑한다.

사용처:

- `vm_do_claim_page()`에서 page와 frame을 연결한 뒤 실제 hardware page table에 등록.
- `writable`은 해당 page가 사용자 쓰기를 허용하는지에 맞춘다.

주의:

- `upage`, `kpage`는 둘 다 page-aligned여야 한다.
- `kpage`는 보통 `palloc_get_page(PAL_USER)`로 얻은 frame의 `kva`다.

### `pml4_clear_page(pml4, upage)`

해당 user page의 present bit를 제거해 매핑을 끊는다.

사용처:

- eviction 후 다음 접근에서 page fault가 다시 나도록 만들 때.
- page 제거 또는 munmap 시 page table에서 매핑을 제거할 때.

### `pml4_is_dirty()`, `pml4_set_dirty()`

해당 page가 수정되었는지 dirty bit를 확인하거나 설정한다.

사용처:

- file-backed page가 evict되거나 munmap될 때 파일에 다시 써야 하는지 판단.
- dirty write-back 후 dirty bit를 정리할 때.

### `pml4_is_accessed()`, `pml4_set_accessed()`

최근 접근 여부를 accessed bit로 확인하거나 설정한다.

사용처:

- eviction victim 선택 알고리즘.
- second-chance 또는 clock 방식에서 자주 사용한다.

주의:

- user virtual address와 kernel virtual address alias 문제가 있을 수 있다. 어느 주소 기준으로 accessed/dirty를 볼지 일관되게 정해야 한다.

## 3. 페이지 할당

관련 파일: `include/threads/palloc.h`, `threads/palloc.c`

### `palloc_get_page(flags)`

한 페이지를 할당하고 kernel virtual address를 반환한다.

자주 쓰는 flag:

- `PAL_USER`: user pool에서 할당.
- `PAL_ZERO`: 내용을 0으로 초기화.
- `PAL_ASSERT`: 실패 시 panic.

사용처:

- VM frame 할당은 반드시 `PAL_USER`를 사용한다.
- zero page나 초기 stack page는 필요에 따라 `PAL_ZERO`를 함께 쓸 수 있다.

주의:

- VM frame을 kernel pool에서 할당하면 커널 메모리가 고갈될 수 있다.
- 최종 VM에서는 user pool 할당 실패 시 eviction으로 frame을 확보해야 한다.

### `palloc_free_page(page)`

`palloc_get_page()`로 얻은 페이지를 반환한다.

사용처:

- frame이 더 이상 어떤 page에도 연결되지 않을 때.
- page claim 실패 시 이미 할당한 frame을 되돌릴 때.

## 4. VM 공통 인터페이스

관련 파일: `include/vm/vm.h`, `vm/vm.c`

### `VM_TYPE(type)`

`VM_ANON | VM_MARKER_0`처럼 marker가 섞인 타입에서 기본 타입만 뽑는다.

사용처:

- `VM_UNINIT`, `VM_ANON`, `VM_FILE` 중 실제 page kind를 구분할 때.
- stack page marker를 붙여도 기본 타입 비교가 가능하게 할 때.

### `page_get_type(page)`

현재 page의 타입을 반환한다.
`VM_UNINIT`인 경우에도 최종 초기화될 타입을 확인할 수 있게 되어 있다.

사용처:

- SPT copy, destroy, swap 처리에서 page 종류별 분기가 필요할 때.

### `swap_in(page, kva)`, `swap_out(page)`, `destroy(page)`

`struct page_operations`에 들어 있는 타입별 함수를 호출하는 매크로.

사용처:

- `vm_do_claim_page()`에서 frame을 매핑한 뒤 `swap_in(page, frame->kva)`.
- eviction에서 victim page를 내보낼 때 `swap_out(page)`.
- SPT kill 또는 page 제거 시 `destroy(page)`.

주의:

- `page->operations`가 올바르게 설정되어 있어야 한다.
- `VM_UNINIT` page의 `swap_in`은 실제 타입 initializer를 호출하는 역할을 한다.

### `vm_alloc_page(type, upage, writable)`

initializer 없이 page를 SPT에 등록하는 축약 매크로.

사용처:

- stack page처럼 별도 lazy loader aux가 필요 없는 anonymous page 등록.

### `vm_alloc_page_with_initializer(type, upage, writable, init, aux)`

lazy page를 만든 뒤 SPT에 넣는 핵심 함수.

사용처:

- executable segment lazy loading.
- mmap file-backed page 등록.
- stack page 등록.

인자 의미:

- `type`: 최종 page 타입. 예: `VM_ANON`, `VM_FILE`, `VM_ANON | VM_MARKER_0`.
- `upage`: user virtual page 시작 주소.
- `writable`: 사용자 쓰기 허용 여부.
- `init`: 실제 page fault 시 frame 내용을 채우는 callback.
- `aux`: `init`에 넘길 보조 정보.

주의:

- 같은 `upage`가 SPT에 이미 있으면 실패해야 한다.
- `aux`는 fault 시점까지 살아 있어야 한다. 동적 할당했다면 lazy loading 완료 후 해제하는 흐름을 정해야 한다.

### `vm_claim_page(va)`

SPT에서 `va`에 해당하는 page를 찾아 실제 frame을 할당하고 매핑한다.

사용처:

- `setup_stack()`에서 초기 stack page를 등록한 뒤 즉시 claim.
- page fault 처리에서 SPT에 존재하는 lazy page를 실제 메모리로 올릴 때.

### `vm_try_handle_fault(f, addr, user, write, not_present)`

page fault가 VM으로 해결 가능한지 판단하고 처리하는 함수.

사용처:

- `userprog/exception.c`의 page fault handler에서 호출.

확인할 조건:

- `addr`가 user virtual address인지.
- not-present fault인지.
- write fault인데 page가 writable인지.
- SPT에 page가 있는지.
- SPT에 없으면 stack growth로 인정 가능한지.

## 5. Supplemental Page Table, SPT

관련 파일: `include/vm/vm.h`, `vm/vm.c`, `include/lib/kernel/hash.h`

현재 `struct supplemental_page_table`은 비어 있다.
보통 내부에 `struct hash`를 두고, page의 `va`를 key로 사용한다.

### `supplemental_page_table_init(spt)`

SPT 내부 자료구조를 초기화한다.

사용처:

- 새 프로세스 생성 시.
- `initd()` 시작 시.
- `fork()`로 자식 프로세스 SPT를 만들 때.

### `spt_find_page(spt, va)`

SPT에서 `va`가 속한 page를 찾는다.

사용처:

- page fault 처리.
- `vm_claim_page()`.
- mmap 범위 중복 검사.
- syscall pointer validation을 lazy page aware하게 바꿀 때.

사용 팁:

- 인자로 들어온 `va`는 페이지 중간 주소일 수 있으므로 `pg_round_down(va)`를 key로 삼는 것이 안전하다.

### `spt_insert_page(spt, page)`

새 page를 SPT에 넣는다.

사용처:

- `vm_alloc_page_with_initializer()`.
- stack growth로 새 stack page를 만들 때.
- mmap으로 file-backed page들을 등록할 때.

주의:

- 중복 주소가 있으면 실패해야 한다.

### `spt_remove_page(spt, page)`

SPT에서 page를 제거하고 관련 자원을 정리한다.

사용처:

- `munmap`.
- SPT kill.
- page 등록 실패 후 rollback.

### `supplemental_page_table_copy(dst, src)`

부모 SPT를 자식 SPT로 복사한다.

사용처:

- `fork()`.

주의:

- COW extra를 하지 않는다면 frame 공유가 아니라 실제 page 내용을 복사하는 방향이 기본이다.
- lazy page는 aux, file handle, writable 정보가 유지되도록 복사해야 한다.

### `supplemental_page_table_kill(spt)`

프로세스 종료 시 SPT가 가진 모든 page를 정리한다.

사용처:

- `process_cleanup()`.

정리 대상:

- page table mapping.
- frame.
- swap slot.
- file-backed dirty page write-back.
- page 구조체 메모리.

## 6. Hash, List, Bitmap

관련 파일: `include/lib/kernel/hash.h`, `include/lib/kernel/list.h`, `include/lib/kernel/bitmap.h`

### Hash API

주요 함수:

- `hash_init`
- `hash_insert`
- `hash_find`
- `hash_delete`
- `hash_apply`
- `hash_destroy`
- `hash_entry`
- `hash_bytes`, `hash_int`

사용처:

- SPT 구현에 가장 적합하다.
- key는 보통 page-aligned user virtual address.

사용 방식:

- `struct page` 안에 `struct hash_elem` 같은 멤버를 추가한다.
- hash function은 page의 `va` 기반.
- less function도 page의 `va` 기반.

### List API

주요 함수:

- `list_init`
- `list_push_back`
- `list_remove`
- `list_begin`, `list_next`, `list_end`
- `list_entry`
- `list_empty`

사용처:

- frame table.
- mmap region 목록.
- eviction 순회 리스트.

사용 방식:

- frame이나 mmap descriptor 구조체에 `struct list_elem`을 넣고 관리한다.

### Bitmap API

주요 함수:

- `bitmap_create`
- `bitmap_scan_and_flip`
- `bitmap_set`
- `bitmap_reset`
- `bitmap_test`
- `bitmap_destroy`

사용처:

- swap slot 사용 여부 추적.

사용 방식:

- bit 하나를 page-sized swap slot 하나로 본다.
- swap out 시 빈 bit를 찾아 mark.
- swap in 후 해당 bit를 reset.

## 7. Uninitialized Page와 Lazy Loading

관련 파일: `include/vm/uninit.h`, `vm/uninit.c`

### `uninit_new(page, va, init, type, aux, initializer)`

아직 실제 타입으로 초기화되지 않은 lazy page를 만든다.

사용처:

- `vm_alloc_page_with_initializer()` 내부.

인자 의미:

- `init`: page fault 시 실제 내용을 채우는 callback.
- `type`: 최종 page 타입.
- `aux`: lazy loader에 넘길 정보.
- `initializer`: 최종 page 타입별 initializer. 예: `anon_initializer`, `file_backed_initializer`.

### `vm_initializer`

타입:

```c
typedef bool vm_initializer (struct page *, void *aux);
```

사용처:

- executable segment lazy loader.
- mmap file lazy loader.

구현 흐름:

- page fault 발생.
- `swap_in()` 호출.
- `VM_UNINIT`의 swap-in이 타입별 initializer를 호출.
- 이후 `init(page, aux)`가 실제 frame 내용을 채운다.

## 8. Anonymous Page와 Swap

관련 파일: `include/vm/anon.h`, `vm/anon.c`, `include/devices/disk.h`

### `vm_anon_init()`

anonymous page subsystem 초기화.

사용처:

- `vm_init()`에서 호출된다.
- swap disk와 swap bitmap 초기화 위치로 적합하다.

현재 코드 기준:

- 로컬 문서에는 `block_get_role(BLOCK_SWAP)`가 언급되지만, 이 프로젝트 코드에는 `devices/block.h`가 없고 `devices/disk.h`가 있다.
- 따라서 현재 코드에서는 `disk_get`, `disk_size`, `disk_read`, `disk_write`를 기준으로 살펴봐야 한다.

### `anon_initializer(page, type, kva)`

page operation을 anonymous page용 operation table로 바꾼다.

사용처:

- `VM_ANON` lazy page가 처음 claim될 때.

### `anon_swap_in(page, kva)`

swap disk에 나가 있던 anonymous page를 frame으로 읽어 온다.

사용처:

- evicted anonymous page가 다시 fault될 때.

### `anon_swap_out(page)`

anonymous page 내용을 swap disk에 기록한다.

사용처:

- frame eviction.

필요한 도구:

- `bitmap_scan_and_flip`으로 swap slot 확보.
- `disk_write`로 sector 단위 기록.
- `pml4_clear_page`로 user mapping 제거.

## 9. File-backed Page와 mmap

관련 파일: `include/vm/file.h`, `vm/file.c`, `include/filesys/file.h`

### `file_backed_initializer(page, type, kva)`

page operation을 file-backed page용 operation table로 바꾼다.

사용처:

- mmap page가 처음 claim될 때.

### `file_backed_swap_in(page, kva)`

파일 내용을 frame으로 읽어 온다.

사용처:

- mmap page에 처음 접근할 때.
- evicted file-backed page를 다시 올릴 때.

필요한 정보:

- file handle.
- file offset.
- read bytes.
- zero bytes.

### `file_backed_swap_out(page)`

file-backed page를 내보낸다.

사용처:

- eviction.
- munmap.
- process exit.

판단 기준:

- dirty page면 `file_write_at`으로 원본 위치에 write-back.
- clean page면 다시 파일에서 읽을 수 있으므로 별도 저장 없이 버릴 수 있다.

### `do_mmap(addr, length, writable, file, offset)`

실제 mmap mapping을 만드는 함수.

사용처:

- `SYS_MMAP` syscall handler에서 fd 검증 후 호출.

검증할 조건:

- `addr != NULL`.
- `addr`는 page-aligned.
- `length > 0`.
- file length가 0이면 실패.
- `offset`은 page-aligned.
- fd 0, 1은 mmap 불가.
- 매핑 범위가 user address 안에 있어야 한다.
- 기존 SPT page와 겹치면 실패.

사용 팁:

- fd의 원본 `struct file *`를 그대로 쓰기보다 `file_reopen()`으로 매핑 전용 handle을 만드는 편이 안전하다.
- 각 page마다 file-backed lazy page를 SPT에 등록한다.

### `do_munmap(addr)`

해당 주소에서 시작하는 mmap mapping을 해제한다.

사용처:

- `SYS_MUNMAP` syscall handler.
- process exit 시 아직 남은 mmap 영역 정리.

필요한 처리:

- dirty page write-back.
- page table mapping 제거.
- frame 해제 또는 page 제거.
- file handle close.
- SPT에서 page 제거.

## 10. File System 헬퍼

관련 파일: `include/filesys/file.h`, `filesys/file.c`

### `file_reopen(file)`

같은 inode를 가리키는 새 file 객체를 연다.

사용처:

- mmap이 원래 fd close와 독립적으로 유지되어야 할 때.
- lazy loader aux가 page fault 시점까지 file을 보관해야 할 때.

### `file_duplicate(file)`

file position과 deny-write 상태까지 복제한다.

사용처:

- `fork()`에서 fd table 복사.

### `file_read_at(file, buffer, size, offset)`

현재 file position을 바꾸지 않고 지정 offset에서 읽는다.

사용처:

- lazy loading.
- file-backed swap-in.
- mmap page load.

### `file_write_at(file, buffer, size, offset)`

현재 file position을 바꾸지 않고 지정 offset에 쓴다.

사용처:

- mmap dirty page write-back.
- file-backed page eviction.

### `file_length(file)`

파일 길이를 반환한다.

사용처:

- mmap 길이 검증.
- 마지막 page의 read/zero byte 계산.

### `file_deny_write(file)`, `file_allow_write(file)`

실행 중인 executable에 쓰기를 막거나 허용한다.

사용처:

- `load()`에서 현재 실행 파일에 `file_deny_write()`가 이미 쓰이고 있다.
- file-backed mmap과 executable deny-write의 상호작용을 볼 때 참고한다.

## 11. Disk와 Swap 관련 헬퍼

관련 파일: `include/devices/disk.h`

### `disk_get(chan_no, dev_no)`

특정 disk device를 가져온다.

사용처:

- swap disk 초기화.

주의:

- 이 코드베이스에서 어떤 channel/device가 swap disk인지 실제 실행 옵션과 `devices/disk.c`를 함께 확인해야 한다.

### `disk_size(disk)`

disk sector 개수를 반환한다.

사용처:

- swap slot 개수 계산.
- slot 하나는 보통 `PGSIZE / DISK_SECTOR_SIZE` sector를 사용한다.

### `disk_read(disk, sector, buffer)`, `disk_write(disk, sector, buffer)`

sector 단위로 disk를 읽고 쓴다.

사용처:

- anonymous page swap-in/swap-out.

주의:

- sector 크기는 `DISK_SECTOR_SIZE`, page 크기는 `PGSIZE`이므로 page 하나를 여러 sector로 나눠 처리해야 한다.

## 12. Syscall과 사용자 포인터 검증

관련 파일: `userprog/syscall.c`, `include/lib/syscall-nr.h`

### `is_valid_user_buffer(buf, size)`

현재 코드에 있는 내부 helper.

기능:

- 사용자 buffer 범위가 user address이고 page table에 매핑되어 있는지 검사한다.

VM 과제에서의 주의:

- lazy page는 아직 `pml4_get_page()`에는 없고 SPT에만 있을 수 있다.
- 따라서 VM 구현 후에는 이 검증이 lazy page를 잘못 거부하지 않는지 확인해야 한다.

### `is_valid_user_string(str)`

현재 코드에 있는 내부 helper.

기능:

- 사용자 문자열이 page boundary를 넘어가도 끝까지 유효한지 검사한다.

VM 과제에서의 주의:

- string이 lazy page에 걸쳐 있으면 page fault로 claim될 수 있어야 한다.

### `SYS_MMAP`, `SYS_MUNMAP`

`include/lib/syscall-nr.h`에는 syscall number가 이미 있다.

사용처:

- `syscall_handler()`의 dispatch에 연결해야 mmap 테스트가 `do_mmap`, `do_munmap`까지 도달한다.

## 13. Page Fault 처리

관련 파일: `userprog/exception.c`

### `rcr2()`

page fault를 일으킨 가상 주소를 읽는다.

사용처:

- `page_fault()`에서 `fault_addr` 계산.

### `intr_frame`

page fault 당시 레지스터와 error code를 담는다.

주요 필드:

- `f->error_code`: not-present, write, user 여부를 bit로 확인.
- `f->rsp`: user mode fault에서는 사용자 stack pointer 판단에 유용.
- `f->rip`: fault를 일으킨 명령어 위치.

주의:

- 현재 `exception.c`는 user fault를 VM 처리 전에 종료시키는 흐름이 있다.
- VM 과제에서는 `vm_try_handle_fault()`가 실제로 호출될 수 있도록 page fault 흐름을 점검해야 한다.

## 14. 디버깅과 테스트 보조

관련 파일: `include/vm/inspect.h`, `vm/inspect.c`, `include/lib/user/syscall.h`

### `register_inspect_intr()`

VM 검사용 interrupt를 등록한다.

사용처:

- `vm_init()`에서 이미 호출된다.

### `get_phys_addr(user_addr)`

사용자 테스트 코드에서 `int 0x42`를 통해 해당 user address가 어느 physical address에 매핑되는지 확인한다.

사용처:

- lazy loading 테스트.
- 여러 가상 주소가 같은 frame을 공유하는지 확인하는 테스트.

주의:

- 일반 구현 코드에서 쓰는 함수가 아니라 테스트/검사용 helper다.

## 15. 자주 쓰이는 조합

### Lazy executable page 등록

흐름:

1. `load_segment()`에서 page별 read/zero 정보를 계산한다.
2. page별 aux를 만든다.
3. `vm_alloc_page_with_initializer(VM_ANON, upage, writable, lazy_load_segment, aux)`로 SPT에 등록한다.
4. 실제 접근 시 page fault가 발생한다.
5. `vm_try_handle_fault()`가 `vm_claim_page()`를 호출한다.
6. `swap_in()`을 통해 lazy loader가 frame 내용을 채운다.

### Page fault 처리

흐름:

1. `rcr2()`로 fault address 획득.
2. error code로 `not_present`, `write`, `user` 계산.
3. `vm_try_handle_fault()` 호출.
4. SPT에서 page 조회.
5. 없으면 stack growth 가능성 판단.
6. 가능하면 `vm_claim_page()`로 frame 할당 및 매핑.

### Eviction

흐름:

1. `palloc_get_page(PAL_USER)` 실패.
2. frame table에서 victim 선택.
3. `swap_out(victim_page)` 호출.
4. page type에 따라 anon은 swap disk, file-backed는 dirty write-back.
5. `pml4_clear_page()`로 page table mapping 제거.
6. 비워진 frame 재사용.

### mmap

흐름:

1. syscall에서 fd, addr, length, offset 검증.
2. `file_reopen()`으로 mapping용 file handle 확보.
3. `do_mmap()`에서 page별 file-backed lazy page 등록.
4. 접근 시 `file_backed_swap_in()` 또는 lazy loader가 file 내용을 읽는다.
5. `munmap()` 또는 exit에서 dirty page를 write-back하고 정리한다.

