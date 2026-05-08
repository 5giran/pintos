# Project 3 기반 리팩토링 기록

## 이번 브랜치의 기준

이번 브랜치 `codex/pr154-project3-review-fixes`에서는 Project 3로 이어가기 위해 리뷰 대응과 직접 리팩토링을 허용했다. 목표는 Project 3 기능을 미리 구현하는 것이 아니라, Project 2 동작을 유지하면서 이후 VM 구현을 얹기 쉬운 책임 경계를 만드는 것이다.

`multi-oom` 후속 디버깅은 이번 범위의 통과 조건이 아니다. 현재 리팩토링은 fork/exec/wait/rox 계열의 기본 동작을 보존하고, `multi-oom`에서 드러날 수 있는 누수나 exit 경로 문제는 별도 작업으로 추적한다.

## Project 2에서 Project 3로 넘어갈 때 위험한 지점

- `process.c`의 executable loading은 file position을 공유하는 `file_seek()` + `file_read()`에 의존하면 fork/exec/load가 겹칠 때 깨지기 쉽다. 실행 파일 segment는 offset 기반으로 읽는 흐름을 기본값으로 둔다.
- `running_file`은 fd table과 다른 ownership을 가진다. 실행 중인 파일의 deny-write lifetime은 exec 성공부터 process cleanup까지 이어져야 하며, fork child도 별도 file object로 이 상태를 이어받아야 한다.
- user pointer 검증이 `pml4_get_page()` 호출마다 흩어져 있으면 lazy page fault를 도입할 때 syscall마다 정책이 달라질 수 있다. Project 3에서는 한 helper에서 SPT 조회와 fault-in 가능 여부를 판단하도록 바꾼다.
- `child_status`처럼 parent와 child가 함께 만지는 record는 ref count와 free 판단을 원자적으로 유지해야 한다. 이 invariant가 깨지면 Project 3 구현 중 반복 fork/exit 테스트에서 디버깅 비용이 커진다.

## 책임 분리 기준

### `process.c`

- process lifetime, fork/exec/wait, executable load, initial stack setup을 담당한다.
- `running_file_close()`, `running_file_install()`, `running_file_duplicate()`에 실행 파일 deny-write ownership을 모은다.
- ELF header, program header, segment body는 `read_file_exact_at()`을 통해 offset 기반으로 읽는다. file position은 syscall의 일반 fd read/write 흐름과 섞이지 않게 둔다.
- argument stack layout은 공개 ABI이므로 Project 3에서도 같은 `argc`, `argv`, fake return address 배치를 유지한다.

### `syscall.c`

- syscall dispatch와 user/kernel copy boundary를 담당한다.
- user buffer 검증은 `validate_user_buffer()` 한 곳에서 read/write 정책을 나눈다.
- Project 3에서는 이 helper의 present-page 판정을 `spt_find_page()`와 `vm_try_handle_fault()` 경로로 연결한다. syscall마다 직접 SPT를 만지는 방식은 피한다.
- `mmap`/`munmap` syscall entry는 Project 3 기능 작업에서 추가한다. 이번 리팩토링은 연결 지점만 만든다.

### `vm/*`

- `struct supplemental_page_table`은 process-owned virtual page map이다.
- `struct page`는 SPT entry, type-specific state, frame 역참조를 가진다.
- `struct frame`은 physical frame table의 entry가 될 준비를 한다. eviction list 정책은 Project 3 기능 작업에서 정한다.

## SPT/hash/page/frame ownership 규칙

- `struct supplemental_page_table.pages`는 `struct hash`로 둔다.
- hash key는 page-aligned user virtual address인 `page->va`다.
- 각 `struct page`는 정확히 하나의 SPT hash에 들어간다.
- `page->spt_elem`은 SPT hash에 들어가 있는 동안 다른 list/hash에 재사용하지 않는다.
- `page->frame`은 현재 page가 claim되어 물리 frame과 연결되어 있을 때만 유효하다.
- `frame->page`는 frame을 점유한 page를 역참조한다.
- `frame->frame_elem`은 이후 frame table 순회와 eviction 후보 선정에 쓸 hook이다. 이번 리팩토링에서는 필드와 주석만 고정한다.

## Lazy loading과 mmap에서 이어받을 결정

- executable lazy loading과 mmap은 모두 file position을 공유하지 않는 `file_read_at()` 기반으로 설계한다.
- VM용 `load_segment()`는 page마다 `file`, `ofs`, `page_read_bytes`, `page_zero_bytes`를 담은 aux 구조체를 넘기는 방향으로 이어간다.
- aux ownership은 page initializer가 가져가고, page가 실제로 claim되거나 destroy될 때 정리하는 쪽이 자연스럽다.
- mmap은 executable loading과 같은 offset/read/zero 계산 규칙을 재사용하되, file-backed page의 dirty write-back 정책은 `vm/file.c`에서 닫는다.

## 잔여 리스크

- `multi-oom`은 여전히 별도 디버깅 대상이다. 이번 리팩토링은 ref count 원자성, running executable lifetime, file offset 기반 loading처럼 기반 결함을 줄이는 데 초점을 맞췄다.
- VM 구현 전까지 `include/vm/vm.h`의 SPT/hash 구조는 선언 수준의 기반이다. 실제 hash 함수, less 함수, insert/find/remove 구현은 Project 3 기능 작업에서 채운다.
- syscall pointer 검증 helper는 아직 Project 2의 present-page 정책을 따른다. Lazy loading 도입 시 이 helper를 먼저 갱신해야 한다.

## 검증 기록

검증은 macOS host가 아니라 x86_64 Ubuntu devcontainer에서 수행했다.

- `make -C pintos/userprog`: PASS
- `fork-once`: PASS
- `fork-multiple`: PASS
- `fork-read`: PASS
- `wait-simple`: PASS
- `wait-twice`: PASS
- `exec-read`: PASS
- `rox-simple`: PASS
- `rox-child`: PASS
- `rox-multichild`: PASS
- `multi-oom`: FAIL, 이번 리팩토링의 통과 조건이 아니며 별도 디버깅 대상으로 남긴다.
- `git diff --check`: PASS
