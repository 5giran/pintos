# Pintos-KAIST Project3: Virtual Memory 및 Appendix 한국어 번역

> 기준 문서: KAIST Pintos GitBook의 `Project3: Virtual Memory` 섹션과 해당 페이지 하단/사이드바의 Appendix 연결 문서.  
> 작성일: 2026-05-07  
> 용도: 개인 학습용 번역/정리본. 원문이 우선이며, 과제 제출물이나 공개 재배포용 문서로 사용하지 마세요.

## 포함 범위

### Project3: Virtual Memory

1. Introduction
2. Memory Management
3. Anonymous Page
4. Stack Growth
5. Memory Mapped Files
6. Swap In/Out
7. Copy-on-Write, COW — Extra
8. FAQ

### Appendix

1. Threads
2. Synchronization
3. Memory Allocation
4. Virtual Address
5. Page Table
6. Debugging Tools
7. Development Tools
8. Hash Table

---

# Project3: Virtual Memory

## 1. Introduction

Project 3에서는 Pintos에 **가상 메모리**를 구현한다. 기존에는 프로세스가 사용하는 메모리가 반드시 물리 메모리에 직접 올라와 있어야 했지만, 가상 메모리 시스템을 갖추면 사용자 프로그램은 실제 물리 메모리보다 큰 주소 공간을 가진 것처럼 동작할 수 있다. 운영체제는 필요한 시점에만 페이지를 메모리에 올리고, 부족하면 일부 페이지를 디스크로 내보내며, 파일을 메모리처럼 매핑할 수도 있다.

이 프로젝트는 Project 2의 사용자 프로그램 기능 위에 구축된다. 따라서 Project 2에서 만든 시스템 호출, 프로세스 실행, 파일 시스템 접근, 사용자 포인터 검증 등의 코드가 정상적으로 동작해야 한다. Project 2에 남아 있는 버그는 Project 3에서 훨씬 더 복잡하게 드러날 수 있으므로, 먼저 Project 2의 문제를 최대한 고쳐야 한다.

### 배경과 소스 파일

가상 메모리 구현은 주로 `vm/` 디렉터리와 관련되어 있다. 중요한 파일은 다음과 같다.

- `include/vm/vm.h`, `vm/vm.c`: 가상 메모리 하위 시스템의 공통 인터페이스와 핵심 함수.
- `include/vm/uninit.h`, `vm/uninit.c`: 아직 실제 타입이 정해지지 않은 페이지, 즉 lazy loading용 초기화되지 않은 페이지를 다룬다.
- `include/vm/anon.h`, `vm/anon.c`: anonymous page를 다룬다. anonymous page는 파일과 직접 연결되지 않은 일반 메모리 페이지다.
- `include/vm/file.h`, `vm/file.c`: file-backed page와 memory-mapped file을 다룬다.
- `include/vm/inspect.h`, `vm/inspect.c`: 채점과 검사에 사용되는 보조 코드다. 일반적으로 수정할 필요가 없다.
- `devices/block.h`, `devices/block.c`: 블록 장치 접근 인터페이스다. swap disk를 다룰 때 사용한다.

`vm.h`에는 페이지 타입을 나타내는 값들이 정의되어 있다. 기본 타입으로 `VM_UNINIT`, `VM_ANON`, `VM_FILE` 등이 있으며, 보조 마커로 `VM_MARKER_0`이 제공된다. 예를 들어 `VM_MARKER_0`은 stack page를 표시하는 용도로 활용할 수 있다. 구현에서는 `VM_TYPE(type)`처럼 타입의 기본 부분만 추출하는 방식이 쓰인다.

### 메모리 용어

**Page**는 가상 메모리의 연속된 영역이다. Pintos에서는 한 페이지의 크기가 4KB이다. 페이지는 page-aligned, 즉 4KB 경계에 맞춰진 가상 주소에서 시작해야 한다.

**Frame**은 물리 메모리의 연속된 영역이다. 크기는 페이지와 동일하게 4KB이며, 실제 데이터가 들어 있는 물리 메모리 공간이다. 사용자 가상 페이지가 실제로 접근되려면 어느 한 frame에 매핑되어 있어야 한다.

**Page table**은 가상 페이지와 물리 frame 사이의 대응 관계를 저장하는 구조다. x86-64에서는 하드웨어가 이 페이지 테이블을 참조하여 가상 주소를 물리 주소로 변환한다. Pintos에서는 각 프로세스마다 자체 page table을 가진다.

**Swap slot**은 swap disk에 있는 페이지 크기의 영역이다. 물리 메모리가 부족할 때, 어떤 가상 page에 매핑되어 있던 physical frame의 내용을 swap slot에 저장하고, 나중에 그 가상 page가 다시 필요해지면 swap slot에서 내용을 읽어 새 frame에 복구한 뒤 해당 virtual page에 매핑한다.

### 자원 관리 개요

가상 메모리 구현에서는 다음 세 가지 자료구조가 핵심이다.

#### Supplemental Page Table, SPT

하드웨어 page table만으로는 Pintos가 구현해야 하는 모든 정보를 담기에 부족하다. 예를 들어 어떤 페이지가 파일에서 로드되어야 하는지, 아직 로드되지 않은 lazy page인지, swap에 나가 있는지, writable인지 등의 정보를 별도로 관리해야 한다. 이 정보를 저장하는 자료구조가 supplemental page table이다.

SPT는 각 프로세스마다 하나씩 갖는 것이 일반적이다. `struct thread` 안에 SPT를 넣고, 프로세스 종료 시 함께 정리한다.

#### Frame Table

frame table은 사용자 pool에서 할당된 물리 frame을 추적한다. 각 frame이 어떤 page와 연결되어 있는지, 어느 프로세스가 사용하는지, eviction 후보가 될 수 있는지 등을 관리한다. 물리 메모리가 부족할 때 어떤 frame을 내보낼지 고르는 데 사용된다.

frame을 할당할 때는 반드시 사용자 페이지용 pool에서 받아야 한다. 즉 `palloc_get_page(PAL_USER)`를 사용해야 한다. kernel pool을 사용자 페이지 저장에 사용하면 커널 메모리가 고갈되어 전체 시스템이 불안정해질 수 있다.

#### Swap Table

swap table은 swap disk의 어느 slot이 사용 중인지 추적한다. swap slot은 lazy하게 할당해야 한다. 즉 페이지가 실제로 swap out될 때 slot을 할당하고, swap in되어 다시 메모리에 올라오면 slot을 해제한다.

### Supplemental Page Table 관리

SPT는 page fault 처리의 중심이다. 사용자가 접근한 가상 주소에 대해 하드웨어 page table에 매핑이 없으면 page fault가 발생한다. 이때 커널은 SPT를 조회해 해당 주소가 유효한 페이지인지 판단한다.

일반적인 page fault 처리 흐름은 다음과 같다.

1. fault address를 페이지 경계로 내린다.
2. 현재 스레드의 SPT에서 해당 page를 찾는다.
3. page가 없으면 stack growth 가능성을 확인한다.
4. page가 존재하지만 아직 frame에 올라와 있지 않으면 frame을 할당한다.
5. page의 타입에 따라 파일에서 읽거나 swap에서 읽거나 zero page로 초기화한다.
6. hardware page table에 가상 주소와 frame을 매핑한다.
7. fault를 일으킨 명령어로 돌아가 실행을 재개한다.

SPT의 구현 방식은 자유롭다. Pintos는 hash table, list, bitmap 등의 자료구조를 제공한다. 대부분의 구현에서는 가상 주소를 key로 하는 hash table이 적합하다.

### Frame Table과 Eviction

사용 가능한 frame이 없으면 eviction을 수행해야 한다. eviction은 기존 frame 하나를 골라 그 내용을 안전하게 보존하거나 버린 뒤, 새 page가 사용할 수 있게 만드는 과정이다.

Eviction의 일반 흐름은 다음과 같다.

1. frame table에서 victim frame을 고른다.
2. victim frame에 연결된 page의 타입을 확인한다.
3. anonymous page라면 내용을 swap disk에 기록한다.
4. file-backed page라면 dirty 여부를 확인한다. 변경된 page는 파일에 다시 기록하고, 변경되지 않았다면 그냥 버릴 수 있다.
5. victim page의 page table 매핑을 제거한다.
6. frame을 새 page에 연결한다.

Victim을 고르는 알고리즘은 자유롭게 선택할 수 있다. 단순한 clock 알고리즘이나 second-chance 알고리즘이 많이 쓰인다. x86-64 page table entry에는 accessed bit와 dirty bit가 있으므로, 이를 이용해 최근 접근 여부와 수정 여부를 판단할 수 있다.

주의할 점은 Pintos의 커널 가상 주소와 사용자 가상 주소가 같은 물리 frame을 alias로 가리킬 수 있다는 점이다. accessed/dirty bit는 실제 접근이 일어난 주소의 PTE에만 반영될 수 있으므로, 구현에서 어느 주소를 기준으로 검사하고 갱신할지 일관되게 정해야 한다.

### Swap Table

Swap은 `--swap-size=n` 옵션으로 만든 swap disk를 사용한다. 예를 들어 다음처럼 실행할 수 있다.

```bash
pintos-mkdisk swap.dsk --swap-size=4
pintos -v -k -T 60 --qemu --disk=filesys.dsk --swap-disk=swap.dsk -- -q run args
```

swap table은 bitmap으로 구현하기 쉽다. 각 bit가 하나의 swap slot을 나타내고, 사용 중이면 1, 비어 있으면 0으로 둔다. anonymous page가 evict될 때 slot을 하나 찾아 쓰고, 다시 swap in되면 slot을 비운다.

### Memory-mapped File 개요

Memory-mapped file은 파일의 내용을 가상 메모리 페이지로 매핑하는 기능이다. 사용자는 `mmap`을 통해 파일을 메모리 주소 범위에 연결하고, 일반 메모리처럼 읽고 쓴다. 운영체제는 필요할 때 파일 내용을 페이지로 읽어 오고, 수정된 페이지는 `munmap`이나 프로세스 종료 시 파일에 다시 기록한다.

예를 들어 파일 하나를 메모리에 매핑한 뒤, 해당 주소를 통해 파일 내용을 읽거나 수정할 수 있다. 이 기능은 파일 I/O와 가상 메모리 시스템이 함께 동작해야 하므로, lazy loading, eviction, dirty bit 관리가 모두 필요하다.

---

## 2. Memory Management

이 단계에서는 가상 메모리의 기본 자료구조와 frame/page 연결 기능을 구현한다.

### Page 구조체

Pintos-KAIST의 VM 설계에서 핵심 객체는 `struct page`다. 이 구조체는 하나의 가상 페이지를 나타내며, 페이지 타입, 가상 주소, 연결된 frame, 페이지별 operation table, 타입별 데이터를 가진다.

대표적인 형태는 다음과 같다.

```c
struct page {
    const struct page_operations *operations;
    void *va;
    struct frame *frame;

    union {
        struct uninit_page uninit;
        struct anon_page anon;
        struct file_page file;
#ifdef EFILESYS
        struct page_cache page_cache;
#endif
    };
};
```

`operations`는 페이지 타입별 동작을 가리킨다. anonymous page와 file-backed page는 swap in/out, destroy 방식이 다르므로, 함수 포인터 테이블을 통해 분기한다.

### Page Operations

페이지별 operation table은 보통 다음 형태다.

```c
struct page_operations {
    bool (*swap_in) (struct page *, void *);
    bool (*swap_out) (struct page *);
    void (*destroy) (struct page *);
    enum vm_type type;
};
```

- `swap_in`: 페이지를 frame으로 읽어 온다.
- `swap_out`: 페이지를 frame에서 내보낸다.
- `destroy`: 페이지가 제거될 때 타입별 자원을 정리한다.
- `type`: 해당 operation table이 어떤 페이지 타입에 속하는지 나타낸다.

### Supplemental Page Table 초기화

`struct supplemental_page_table`은 각 프로세스의 가상 페이지 정보를 저장한다. 보통 hash table을 내부 자료구조로 사용한다.

```c
void supplemental_page_table_init (struct supplemental_page_table *spt);
```

이 함수는 SPT 내부 자료구조를 초기화한다. 새 프로세스가 생성될 때, 또는 `struct thread`가 초기화될 때 호출해야 한다.

### Page 검색

```c
struct page *spt_find_page (struct supplemental_page_table *spt, void *va);
```

이 함수는 SPT에서 `va`가 속한 page를 찾는다. `va`는 반드시 페이지 경계에 맞춰져 있지 않을 수 있으므로, 내부에서 `pg_round_down(va)`를 적용해 key로 사용하는 것이 안전하다.

### Page 삽입

```c
bool spt_insert_page (struct supplemental_page_table *spt, struct page *page);
```

이 함수는 새 page를 SPT에 넣는다. 같은 가상 주소의 page가 이미 존재하면 실패해야 한다. 성공하면 `true`, 중복이나 메모리 부족 등으로 실패하면 `false`를 반환한다.

### Frame 구조체

Frame은 실제 물리 메모리 한 페이지를 나타낸다. 일반적인 형태는 다음과 같다.

```c
struct frame {
    void *kva;
    struct page *page;
};
```

- `kva`: 커널 가상 주소로 접근 가능한 frame 주소.
- `page`: 이 frame에 올라와 있는 가상 page.

구현에 따라 frame table 연결용 list element, lock, owner thread 정보 등을 추가할 수 있다.

### Frame 할당

```c
static struct frame *vm_get_frame (void);
```

이 함수는 사용자 pool에서 새 frame을 가져온다. 반드시 `PAL_USER`를 사용해야 한다.

```c
void *kva = palloc_get_page(PAL_USER);
```

할당에 실패하면 eviction을 수행해 frame 하나를 확보해야 한다. 초기 단계에서는 panic을 발생시켜도 일부 테스트를 통과할 수 있지만, 최종 구현에서는 eviction을 제대로 구현해야 한다.

### Page Claim

```c
bool vm_claim_page (void *va);
static bool vm_do_claim_page (struct page *page);
```

`vm_claim_page`는 가상 주소를 받아 SPT에서 page를 찾고, 실제 frame을 할당해 해당 page를 claim한다. 내부적으로 `vm_do_claim_page`가 frame 할당, page-frame 연결, hardware page table 매핑, swap in을 수행한다.

일반적인 흐름은 다음과 같다.

1. `spt_find_page`로 page를 찾는다.
2. `vm_get_frame`으로 frame을 얻는다.
3. `frame->page = page`, `page->frame = frame`으로 연결한다.
4. `pml4_set_page`로 사용자 가상 주소와 frame의 `kva`를 매핑한다.
5. `swap_in(page, frame->kva)`를 호출해 내용을 채운다.

매핑과 swap-in의 순서는 구현에 따라 조정할 수 있지만, 실패 시 연결과 page table 상태를 되돌리는 처리가 필요하다.

---

## 3. Anonymous Page

Anonymous page는 특정 파일과 직접 연결되지 않은 메모리 페이지다. 일반적인 heap, stack, zero-initialized page가 여기에 해당한다. 이 단계에서는 lazy loading, executable segment loading, stack 초기화, page fault 기반 로딩을 구현한다.

### Lazy Loading의 개념

기존 Project 2에서는 실행 파일의 segment를 프로세스 시작 시 즉시 메모리에 로드했다. Project 3에서는 이를 lazy하게 바꾼다. 즉, segment의 각 page에 대해 “나중에 fault가 나면 이 파일의 이 offset에서 이만큼 읽고 나머지는 0으로 채우라”는 정보만 SPT에 등록한다. 실제 파일 읽기는 page fault가 발생했을 때 수행한다.

이 접근은 불필요한 I/O를 줄이고, 큰 실행 파일이나 많은 페이지를 가진 프로세스에서 메모리 사용을 줄인다.

### VM_UNINIT과 Initializer

아직 실제 타입으로 초기화되지 않은 page는 `VM_UNINIT` 타입이다. 이 page는 initializer를 가진다. page fault가 발생해 실제 frame이 필요해지면 initializer가 호출되어 page를 `VM_ANON` 또는 `VM_FILE` 등 실제 타입으로 바꾸고 내용을 채운다.

핵심 함수는 다음과 같다.

```c
bool vm_alloc_page_with_initializer (
    enum vm_type type,
    void *upage,
    bool writable,
    vm_initializer *init,
    void *aux
);
```

이 함수는 주어진 가상 주소에 uninitialized page를 만들고 SPT에 넣는다. `type`은 최종적으로 초기화될 타입이다. `init`은 실제 로딩 함수이며, `aux`는 로딩에 필요한 보조 데이터다.

구현 시 확인할 점은 다음과 같다.

- `upage`가 이미 SPT에 있으면 실패해야 한다.
- 새 `struct page`를 할당하고 `uninit_new` 또는 유사한 초기화 함수를 호출한다.
- writable 여부를 저장해야 한다.
- SPT 삽입에 실패하면 할당한 page를 해제해야 한다.

### Uninit Page 초기화

```c
bool uninit_initialize (struct page *page, void *kva);
```

이 함수는 `VM_UNINIT` page가 실제로 fault되었을 때 호출된다. 내부에서 저장된 initializer를 실행하고, page의 operation table을 실제 타입의 operation table로 바꾼다. 예를 들어 anonymous page라면 `anon_initializer`가 호출된다.

### Anonymous Page 초기화

```c
void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);
```

`vm_anon_init`은 anonymous page 하위 시스템을 초기화한다. 이후 swap 기능을 구현할 때 swap disk와 bitmap 초기화도 이쪽에서 처리할 수 있다.

`anon_initializer`는 page를 anonymous page로 초기화하고, page operation을 anonymous operation table로 설정한다. 기본 anonymous page는 frame이 새로 할당되었을 때 0으로 채워지는 것이 일반적이다. 실행 파일 segment처럼 파일에서 읽어야 하는 경우에는 uninit page의 lazy loader가 실제 파일 내용을 채운다.

### 실행 파일 Segment Lazy Loading

Project 2의 `load_segment`는 파일 내용을 즉시 메모리에 읽어 왔다. Project 3에서는 각 page에 대해 lazy loading 정보를 만들고 `vm_alloc_page_with_initializer`를 호출해야 한다.

```c
static bool load_segment (
    struct file *file,
    off_t ofs,
    uint8_t *upage,
    uint32_t read_bytes,
    uint32_t zero_bytes,
    bool writable
);
```

각 page마다 다음 정보를 저장해야 한다.

- 파일 객체
- 파일 offset
- 읽어야 할 byte 수
- 0으로 채워야 할 byte 수
- writable 여부

이 정보는 `aux` 구조체에 담아 lazy loader로 넘긴다.

```c
static bool lazy_load_segment (struct page *page, void *aux);
```

이 함수는 실제 page fault 시 호출된다. `aux`에서 파일, offset, read_bytes, zero_bytes를 꺼낸 뒤 frame의 `kva`에 파일 내용을 읽고 나머지를 0으로 채운다.

주의할 점은 `aux`의 수명이다. 각 page마다 별도의 aux를 동적 할당하는 방식이 안전하다. lazy loading이 끝나면 aux를 해제해야 한다. 파일 객체도 page fault 시점까지 유효해야 하므로, 필요한 경우 `file_reopen`을 사용해 독립적인 파일 핸들을 보관한다.

### Stack 설정

초기 stack도 lazy하게 만들 수 있다. `setup_stack`에서는 stack 최상단 바로 아래의 page를 `VM_ANON | VM_MARKER_0` 같은 타입으로 등록하고, 실제 접근이 일어나면 frame을 할당한다.

```c
static bool setup_stack (struct intr_frame *if_);
```

초기 stack 주소는 `USER_STACK`이며, 첫 stack page는 `USER_STACK - PGSIZE`에 위치한다. 이후 `argument_stack` 등에서 stack에 값을 쓰면 page fault가 발생하고, 해당 page가 실제로 claim된다.

### Page Fault 처리

```c
bool vm_try_handle_fault (
    struct intr_frame *f,
    void *addr,
    bool user,
    bool write,
    bool not_present
);
```

이 함수는 page fault가 VM으로 해결 가능한지 판단한다. 기본적인 조건은 다음과 같다.

- 주소가 user virtual address여야 한다.
- fault가 protection violation이 아니라 not-present fault여야 한다.
- 해당 주소의 page가 SPT에 있거나, stack growth로 인정될 수 있어야 한다.
- write fault인데 page가 writable이 아니면 실패해야 한다.

해결 가능한 fault라면 `vm_claim_page`를 호출해 page를 frame에 올린다.

### SPT 복사와 정리

`fork`를 지원하려면 부모의 SPT를 자식에게 복사해야 한다.

```c
bool supplemental_page_table_copy (
    struct supplemental_page_table *dst,
    struct supplemental_page_table *src
);
```

초기 구현에서는 부모의 각 page를 순회하며 자식에게 같은 타입의 page를 만들고 내용을 복사한다. COW extra를 구현하지 않는다면 frame을 공유하지 않고 실제 데이터를 복사해야 한다.

프로세스 종료 시에는 SPT의 모든 page를 제거하고 타입별 destroy 함수를 호출해야 한다.

```c
void supplemental_page_table_kill (struct supplemental_page_table *spt);
```

정리 과정에서는 다음을 처리해야 한다.

- page table mapping 제거
- frame 해제
- swap slot 해제
- file-backed page의 dirty write-back
- page 구조체 메모리 해제

---

## 4. Stack Growth

Stack growth는 사용자가 현재 stack 아래쪽 주소에 접근했을 때, 운영체제가 이를 정상적인 stack 확장으로 판단하고 새 stack page를 할당하는 기능이다.

### Stack Growth 판단

모든 잘못된 주소 접근을 stack growth로 인정하면 안 된다. 보통 다음 조건을 함께 사용한다.

- 접근 주소가 user address여야 한다.
- 접근 주소가 현재 stack pointer 근처여야 한다.
- 접근 주소가 정해진 stack 최대 크기 안에 있어야 한다.

x86-64에서는 `PUSH`, `PUSHA` 같은 명령이 stack pointer보다 약간 아래 주소에 먼저 접근할 수 있다. 따라서 fault address가 `rsp - 8` 또는 `rsp - 32` 근처인 경우도 stack growth로 인정하는 관례가 있다.

### Kernel Mode Page Fault와 rsp

page fault가 사용자 모드에서 발생하면 `struct intr_frame`의 `rsp`가 사용자 stack pointer를 담고 있다. 하지만 커널 모드에서 사용자 주소를 접근하다가 fault가 날 수도 있다. 이 경우 `f->rsp`는 kernel stack pointer이므로 그대로 사용하면 안 된다.

따라서 syscall 진입 시 사용자 `rsp`를 `struct thread`에 저장해 두고, kernel mode fault에서는 저장된 사용자 `rsp`를 기준으로 stack growth를 판단하는 방식이 필요하다.

### 구현 함수

```c
static void vm_stack_growth (void *addr);
```

이 함수는 `addr`이 속한 page를 stack page로 할당하고 claim한다. 주소는 `pg_round_down(addr)`로 페이지 경계에 맞춰야 한다.

```c
bool vm_try_handle_fault (...);
```

이 함수 안에서 SPT에 page가 없을 때 stack growth 가능성을 판단하고, 가능하면 `vm_stack_growth`를 호출한다.

### Stack 크기 제한

Pintos-KAIST 문서에서는 stack 크기를 제한해야 한다. 일반적으로 1MB 제한을 사용한다. 즉 stack은 `USER_STACK`에서 아래쪽으로 최대 1MB까지만 자라야 한다.

---

## 5. Memory Mapped Files

Memory-mapped file은 파일을 프로세스의 가상 주소 공간에 매핑한다. 사용자는 `mmap`으로 파일을 주소에 연결하고, `munmap`으로 연결을 해제한다.

### mmap 시스템 호출

```c
void *mmap (void *addr, size_t length, int writable, int fd, off_t offset);
```

성공하면 매핑된 시작 주소를 반환하고, 실패하면 `NULL` 또는 지정된 실패 값을 반환한다.

검증해야 할 조건은 다음과 같다.

- `addr`은 page-aligned여야 한다.
- `addr`은 0이 아니어야 한다.
- `length`는 0보다 커야 한다.
- 파일 길이가 0이면 실패해야 한다.
- `offset`은 page-aligned여야 한다.
- 매핑하려는 주소 범위가 기존 page와 겹치면 실패해야 한다.
- console file descriptor, 즉 stdin/stdout은 매핑할 수 없다.
- 주소 범위는 user virtual address 안에 있어야 한다.

### do_mmap

```c
void *do_mmap (
    void *addr,
    size_t length,
    int writable,
    struct file *file,
    off_t offset
);
```

이 함수는 실제 매핑을 만든다. 각 page마다 file-backed lazy page를 SPT에 등록한다. 마지막 page는 파일에서 읽을 byte 수가 PGSIZE보다 작을 수 있고, 나머지는 0으로 채워야 한다.

매핑에는 원래 file descriptor의 파일 객체를 그대로 쓰기보다 `file_reopen`으로 독립적인 파일 객체를 만드는 것이 안전하다. 그래야 사용자가 원래 fd를 닫아도 매핑은 계속 유효하게 유지된다.

### File-backed Page 초기화

file-backed page의 lazy loader는 page fault 시점에 파일에서 내용을 읽어 frame에 채운다.

필요한 aux 정보는 보통 다음과 같다.

- reopen된 file 객체
- file offset
- read_bytes
- zero_bytes
- writable 여부

file-backed page의 swap-in은 파일에서 읽거나, swap out된 상태라면 swap에서 읽도록 구현할 수 있다. 기본 memory-mapped file 구현에서는 evicted file-backed page가 dirty이면 파일에 기록하고, dirty가 아니면 단순히 버린 뒤 나중에 다시 파일에서 읽을 수 있다.

### munmap

```c
void munmap (void *addr);
void do_munmap (void *addr);
```

`munmap`은 `addr`에서 시작하는 매핑을 해제한다. 매핑된 page들을 순회하며 dirty page는 파일에 다시 기록하고, page table mapping과 SPT entry를 제거한다.

주의할 점은 `munmap`이 정확히 매핑 시작 주소에 대해 호출된다고 가정하더라도, 구현에서는 해당 매핑의 길이를 알아야 전체 page를 해제할 수 있다는 점이다. 따라서 mapping 단위의 metadata를 별도로 저장하거나, 각 file-backed page에 같은 mapping 정보를 연결하는 방식이 필요하다.

### Dirty Write-back

파일에 다시 써야 하는 경우는 page가 수정되었을 때다. 이를 판단하기 위해 page table의 dirty bit를 확인한다.

- dirty이면 파일의 해당 offset에 `read_bytes`만큼 기록한다.
- zero padding 영역은 파일에 쓸 필요가 없다.
- write-back 후에는 dirty bit를 지울 수 있다.

파일을 닫거나 삭제해도 매핑은 즉시 사라지지 않는다. Unix 관례처럼, `munmap`되거나 프로세스가 종료될 때까지 매핑은 유효해야 한다.

---

## 6. Swap In/Out

Swap은 물리 메모리가 부족할 때 page 내용을 디스크에 임시 저장하는 기능이다. Project 3의 최종 단계에서는 anonymous page와 file-backed page가 eviction될 수 있어야 한다.

### Swap Disk 초기화

swap disk는 Pintos 실행 시 별도 block device로 제공된다. 초기화 시 `block_get_role(BLOCK_SWAP)`으로 swap block을 얻고, swap slot bitmap을 만든다. slot 하나는 페이지 하나를 저장할 수 있어야 하므로, slot 크기는 `PGSIZE / BLOCK_SECTOR_SIZE` sector가 된다.

### Anonymous Page Swap-out

anonymous page는 파일 원본이 없으므로, eviction 시 내용을 잃지 않으려면 swap disk에 반드시 기록해야 한다.

흐름은 다음과 같다.

1. swap bitmap에서 빈 slot을 찾는다.
2. frame의 `kva` 내용을 swap disk의 해당 slot에 sector 단위로 기록한다.
3. page에 swap slot index를 저장한다.
4. page table mapping을 제거한다.
5. frame을 해제하거나 재사용한다.

swap slot이 없으면 더 이상 안전하게 eviction할 수 없다. 이 경우 kernel panic 또는 프로세스 종료 등의 처리가 필요하다.

### Anonymous Page Swap-in

swap out된 anonymous page에 다시 접근하면 page fault가 발생한다. VM은 page의 swap slot 정보를 보고 swap disk에서 내용을 읽어 frame에 복원한다.

흐름은 다음과 같다.

1. frame을 할당한다.
2. swap disk에서 slot 내용을 읽어 frame에 채운다.
3. bitmap에서 해당 slot을 해제한다.
4. page의 swap slot 정보를 초기화한다.
5. page table에 다시 매핑한다.

### File-backed Page Swap-out

file-backed page는 원본 파일이 있으므로 dirty 여부에 따라 처리한다.

- page가 dirty라면 파일의 대응 offset에 내용을 기록한다.
- dirty가 아니라면 파일에서 다시 읽을 수 있으므로 디스크에 별도 저장하지 않아도 된다.

일부 구현에서는 file-backed page도 swap disk에 저장할 수 있지만, 문서의 기본 방향은 memory-mapped file의 dirty page를 파일에 write-back하는 것이다.

### Eviction과 Page Table 정리

page를 swap out한 뒤에는 해당 user virtual address의 page table entry를 제거해야 한다. 그래야 다음 접근에서 page fault가 발생하고, swap-in 또는 file reload가 수행된다.

---

## 7. Copy-on-Write, COW — Extra

Copy-on-Write는 extra 과제다. 기본 `fork`에서는 부모의 모든 page 내용을 자식에게 복사한다. COW를 구현하면 부모와 자식이 처음에는 같은 frame을 공유하고, 둘 중 하나가 write를 시도할 때만 실제 복사를 수행한다.

### 기본 아이디어

1. `fork` 시 부모와 자식의 page table이 같은 frame을 가리키도록 한다.
2. 두 mapping을 모두 read-only로 바꾼다.
3. page metadata에 COW 상태와 참조 수를 기록한다.
4. write fault가 발생하면 새 frame을 할당하고 기존 내용을 복사한다.
5. fault를 일으킨 프로세스의 mapping만 writable 새 frame으로 바꾼다.
6. 기존 frame의 참조 수를 줄이고, 0이 되면 해제한다.

### 구현 시 주의점

COW는 일반 anonymous page뿐 아니라 file-backed page와도 상호작용할 수 있다. 하지만 extra 테스트의 요구 범위는 fork에서 발생하는 COW에 초점이 있다.

write-protect된 page에서 발생하는 fault는 not-present fault가 아니라 protection fault일 수 있다. 따라서 `vm_try_handle_fault`에서 COW fault를 별도로 인식해야 한다.

참조 수와 frame 공유 상태는 lock으로 보호해야 한다. 여러 프로세스가 동시에 같은 COW frame에 접근하거나 종료될 수 있기 때문이다.

---

## 8. FAQ

### Project 2가 필요한가?

필요하다. Project 3은 Project 2 위에 구축된다. 시스템 호출, 프로세스 실행, 파일 descriptor, 사용자 포인터 처리 등이 정상이어야 한다.

### page fault handler에서 돌아오면 어디서 실행이 재개되는가?

page fault가 성공적으로 처리되면 fault를 일으킨 명령어가 다시 실행된다. 예를 들어 아직 로드되지 않은 주소를 읽으려다 fault가 났다면, handler가 page를 claim하고 내용을 로드한 뒤 같은 load 명령이 재시도된다.

### 데이터 세그먼트도 stack처럼 자라야 하는가?

아니다. stack growth만 구현하면 된다. heap 확장은 별도 시스템 호출이나 allocator 정책의 문제이며, Project 3의 stack growth와 동일하게 취급하지 않는다.

### 왜 `PAL_USER`를 써야 하는가?

사용자 page frame은 user pool에서 할당되어야 한다. kernel pool을 사용하면 커널의 중요한 메모리 할당이 실패할 수 있다. 테스트에서도 `-ul` 옵션으로 user pool 크기를 제한하여 eviction이 제대로 동작하는지 확인한다.

---

# Appendix

## A. Threads

Pintos의 thread는 `struct thread`로 표현된다. 각 thread는 별도의 kernel stack을 가지며, `struct thread`는 그 stack page의 아래쪽에 배치된다. stack은 같은 page의 위쪽에서 아래쪽으로 자란다.

이 배치 때문에 `struct thread`가 너무 커지면 kernel stack 공간이 줄어든다. 큰 배열이나 큰 구조체를 `struct thread`에 직접 넣거나 kernel stack의 지역 변수로 두는 것은 위험하다. 일반적으로 `struct thread`는 1KB보다 작게 유지하는 것이 좋다.

### struct thread의 주요 필드

- `tid`: thread identifier.
- `status`: thread 상태. `THREAD_RUNNING`, `THREAD_READY`, `THREAD_BLOCKED`, `THREAD_DYING` 등이 있다.
- `name`: 디버깅용 thread 이름.
- `tf`: interrupt frame. context switch와 사용자 모드 진입/복귀에 사용된다.
- `priority`: scheduler가 사용하는 우선순위.
- `elem`: ready list나 semaphore waiters list 등에 들어가기 위한 list element.
- `pml4`: 해당 thread/process의 page table.
- `magic`: stack overflow를 감지하기 위한 sentinel 값.

### Thread 상태

`THREAD_RUNNING`은 현재 CPU에서 실행 중인 thread다. 단일 CPU Pintos에서는 한 번에 하나만 존재한다.

`THREAD_READY`는 실행 가능하지만 CPU를 기다리는 thread다. ready list에 들어 있다.

`THREAD_BLOCKED`는 semaphore, lock, I/O, sleep 등 어떤 사건을 기다리며 실행할 수 없는 상태다.

`THREAD_DYING`은 종료 중이며 scheduler가 정리할 thread다.

### 주요 함수

```c
void thread_init (void);
void thread_start (void);
tid_t thread_create (const char *name, int priority, thread_func *function, void *aux);
void thread_block (void);
void thread_unblock (struct thread *t);
void thread_yield (void);
void thread_exit (void) NO_RETURN;
struct thread *thread_current (void);
```

`thread_init`은 threading system을 초기화한다. `thread_start`는 idle thread를 만들고 scheduler를 시작한다.

`thread_create`는 새 kernel thread를 생성한다. 새 thread는 `function(aux)`를 실행한다.

`thread_block`은 현재 thread를 block 상태로 바꾸고 CPU를 양보한다. interrupt가 꺼진 상태에서 호출해야 한다.

`thread_unblock`은 block된 thread를 ready 상태로 바꾼다.

`thread_yield`는 현재 thread가 CPU를 자발적으로 양보하게 한다.

`thread_exit`는 현재 thread를 종료한다.

`thread_current`는 현재 실행 중인 thread의 `struct thread *`를 반환한다.

### Kernel Stack 주의사항

Pintos의 kernel stack은 크기가 작다. 따라서 다음을 피해야 한다.

- 큰 배열을 지역 변수로 선언하기.
- 재귀 호출을 깊게 사용하기.
- 큰 구조체를 값으로 복사하기.

큰 데이터는 `malloc`, `palloc_get_page`, static/global storage 등을 사용해 별도로 관리하는 것이 안전하다.

---

## B. Synchronization

Pintos는 선점형 kernel thread를 사용하므로 공유 자료구조에 접근할 때 동기화가 필요하다. 특히 ready list, semaphore waiters, file system 구조, frame table, supplemental page table, swap table 등은 여러 thread가 동시에 접근할 수 있다.

### Interrupt 비활성화

가장 낮은 수준의 동기화 방법은 interrupt를 끄는 것이다. interrupt가 꺼져 있으면 timer interrupt로 인한 선점이 일어나지 않으므로 현재 CPU에서 실행 중인 thread가 중간에 빼앗기지 않는다.

```c
enum intr_level intr_disable (void);
enum intr_level intr_enable (void);
enum intr_level intr_set_level (enum intr_level level);
enum intr_level intr_get_level (void);
```

`intr_disable`은 interrupt를 끄고 이전 상태를 반환한다. 보통 다음처럼 사용한다.

```c
enum intr_level old_level = intr_disable ();
/* critical section */
intr_set_level (old_level);
```

하지만 interrupt를 오래 끄면 timer, I/O, scheduler 반응성이 떨어진다. 따라서 짧은 critical section에만 사용해야 한다. 긴 연산이나 block될 수 있는 연산을 interrupt-off 상태에서 수행하면 안 된다.

### Semaphore

Semaphore는 정수 값과 waiters list를 가진 동기화 객체다.

```c
void sema_init (struct semaphore *sema, unsigned value);
void sema_down (struct semaphore *sema);
bool sema_try_down (struct semaphore *sema);
void sema_up (struct semaphore *sema);
```

`sema_down`은 semaphore 값이 양수가 될 때까지 기다린 뒤 값을 1 감소시킨다. 값이 0이면 현재 thread는 block된다.

`sema_up`은 값을 1 증가시키고, 기다리는 thread가 있으면 하나를 깨운다. `sema_up`은 interrupt handler에서도 호출할 수 있다.

Semaphore는 lock 구현의 기반이며, 특정 사건이 발생할 때까지 thread를 재우는 데도 사용할 수 있다.

### Lock

Lock은 한 번에 하나의 thread만 소유할 수 있는 mutual exclusion 도구다.

```c
void lock_init (struct lock *lock);
void lock_acquire (struct lock *lock);
bool lock_try_acquire (struct lock *lock);
void lock_release (struct lock *lock);
bool lock_held_by_current_thread (const struct lock *lock);
```

lock은 소유자가 있으므로 acquire한 thread만 release해야 한다. 같은 thread가 이미 가진 lock을 다시 acquire하려 하면 안 된다.

공유 자료구조를 보호할 때는 lock이 가장 일반적인 선택이다. 예를 들어 frame table 전체를 하나의 lock으로 보호하거나, 더 정교하게 각 frame 또는 hash bucket별 lock을 둘 수 있다.

### Condition Variable과 Monitor

Condition variable은 lock과 함께 사용된다. 어떤 조건이 만족될 때까지 기다리고, 다른 thread가 조건 변화를 알릴 수 있게 한다.

```c
void cond_init (struct condition *cond);
void cond_wait (struct condition *cond, struct lock *lock);
void cond_signal (struct condition *cond, struct lock *lock);
void cond_broadcast (struct condition *cond, struct lock *lock);
```

`cond_wait`는 현재 lock을 원자적으로 release하고 block한다. 깨어나면 다시 lock을 acquire한 뒤 반환한다. 조건이 실제로 만족되는지는 반드시 while loop로 다시 확인해야 한다.

```c
lock_acquire (&lock);
while (!condition)
    cond_wait (&cond, &lock);
/* condition is true */
lock_release (&lock);
```

이 패턴은 monitor 스타일 동기화의 기본이다.

### Optimization Barrier

컴파일러는 성능 최적화를 위해 메모리 접근 순서를 바꿀 수 있다. 하드웨어나 interrupt handler와 공유되는 변수처럼 순서가 중요한 경우에는 optimization barrier가 필요할 수 있다.

```c
barrier ();
```

`barrier`는 컴파일러가 barrier 전후의 메모리 접근을 재배치하지 못하게 한다. 단, CPU cache coherence나 atomicity를 보장하는 것은 아니다.

`volatile`은 일반적인 동기화 수단이 아니다. 대부분의 경우 lock, semaphore, interrupt disable 같은 명시적인 동기화가 필요하다. `volatile`은 memory-mapped I/O 또는 매우 특수한 경우에만 제한적으로 사용해야 한다.

### 동기화 설계 팁

Project 3에서는 다음 자료구조에 동기화가 필요할 수 있다.

- frame table
- swap table
- file system 접근
- supplemental page table
- per-page metadata
- COW 참조 수

처음에는 coarse-grained lock으로 정확성을 확보하는 것이 좋다. 이후 필요하면 lock 범위를 줄인다. 너무 이른 fine-grained locking은 deadlock과 race condition을 만들기 쉽다.

---

## C. Memory Allocation

Pintos는 두 종류의 kernel memory allocator를 제공한다.

1. Page allocator: 4KB page 단위로 할당한다.
2. Block allocator: 작은 크기 메모리를 malloc/free 스타일로 할당한다.

### Page Allocator

Page allocator는 메모리를 page 단위로 관리한다. kernel pool과 user pool이 분리되어 있다.

```c
void *palloc_get_page (enum palloc_flags flags);
void *palloc_get_multiple (enum palloc_flags flags, size_t page_cnt);
void palloc_free_page (void *page);
void palloc_free_multiple (void *pages, size_t page_cnt);
```

중요한 flag는 다음과 같다.

- `PAL_ASSERT`: 할당 실패 시 panic.
- `PAL_ZERO`: 할당된 page를 0으로 채운다.
- `PAL_USER`: user pool에서 할당한다.

Project 3의 사용자 frame은 반드시 `PAL_USER`로 할당해야 한다.

Page allocator는 bitmap을 사용해 free page를 관리한다. 여러 page를 연속으로 할당해야 할 때는 first-fit 방식으로 연속 영역을 찾는다. 따라서 메모리 단편화가 발생하면 전체 free page 수가 충분해도 큰 연속 할당은 실패할 수 있다.

할당된 page는 보통 `0xcc` 패턴으로 채워져 있을 수 있다. 이는 초기화되지 않은 메모리 사용을 디버깅하기 쉽게 하기 위한 값이다. `PAL_ZERO`를 쓰면 0으로 초기화된다.

Interrupt context에서는 page allocation이 허용되지 않는다. 할당 과정에서 lock이나 block 가능한 동작이 필요할 수 있기 때문이다.

### Block Allocator

Block allocator는 `malloc`, `calloc`, `realloc`, `free`를 제공한다.

```c
void *malloc (size_t size);
void *calloc (size_t a, size_t b);
void *realloc (void *block, size_t new_size);
void free (void *block);
```

작은 allocation은 size class별 arena에서 관리된다. 큰 allocation은 page allocator를 사용해 여러 page를 직접 할당한다.

작은 객체를 많이 만들 때는 `malloc`이 편리하지만, page 단위 정렬이나 사용자 frame이 필요한 경우에는 `palloc_get_page`를 사용해야 한다.

### Fragmentation

Block allocator와 page allocator 모두 단편화 문제가 있다. 특히 장시간 실행되는 테스트나 eviction이 많은 상황에서는 다음을 주의해야 한다.

- 필요 없는 page/frame/page metadata를 즉시 해제한다.
- `malloc`으로 만든 aux 구조체는 lazy loading 후 반드시 해제한다.
- 실패 경로에서도 할당한 자원을 되돌린다.
- page와 frame의 소유 관계를 명확히 한다.

### VM 구현에서의 사용 기준

- `struct page`, aux 구조체, mapping metadata: 보통 `malloc`/`free`.
- 실제 사용자 데이터 frame: `palloc_get_page(PAL_USER)`.
- kernel 전용 임시 page나 큰 buffer: 상황에 따라 `palloc_get_page`.

---

## D. Virtual Address

Pintos-KAIST는 x86-64의 64-bit 가상 주소 체계를 사용한다. 사용자 주소 공간과 커널 주소 공간은 `KERN_BASE`를 기준으로 나뉜다.

### Page 관련 상수

```c
#define PGSIZE 4096
#define PGBITS 12
#define PGMASK BITMASK(PGBITS)
#define PGSHIFT PGBITS
```

- `PGSIZE`: 한 page의 byte 크기.
- `PGBITS`: page offset을 표현하는 bit 수.
- `PGMASK`: page offset 부분을 추출하는 mask.

### 주소 보조 함수

```c
static inline unsigned pg_ofs (const void *va);
static inline uint64_t pg_no (const void *va);
static inline void *pg_round_down (const void *va);
static inline void *pg_round_up (const void *va);
```

`pg_ofs`는 주소의 page offset을 반환한다. `pg_no`는 page number를 반환한다.

`pg_round_down`은 주소를 해당 page의 시작 주소로 내린다. SPT key를 만들 때 거의 항상 사용한다.

`pg_round_up`은 주소를 다음 page 경계로 올린다.

### User Address와 Kernel Address

```c
#define KERN_BASE 0x8004000000
```

`KERN_BASE` 아래는 user virtual address, 그 이상은 kernel virtual address로 취급된다.

```c
bool is_user_vaddr (const void *va);
bool is_kernel_vaddr (const void *va);
```

사용자 프로그램이 넘긴 포인터는 반드시 user address인지 확인해야 한다. kernel address를 사용자 포인터로 받아 역참조하면 보안과 안정성 문제가 생긴다.

### Physical Address 변환

```c
void *ptov (uintptr_t pa);
uintptr_t vtop (const void *va);
```

`ptov`는 물리 주소를 kernel virtual address로 변환한다. `vtop`은 kernel virtual address를 물리 주소로 변환한다. 이 함수들은 커널 direct mapping 영역을 전제로 한다.

### Page Table Entry Helper

x86-64 page table entry에는 present, writable, user, accessed, dirty 등의 bit가 있다. Pintos는 이 bit를 다루는 helper를 제공한다.

가상 메모리 구현에서 특히 중요한 bit는 다음 두 가지다.

- accessed bit: page가 최근 접근되었는지 확인하는 데 사용한다.
- dirty bit: page가 수정되었는지 확인하는 데 사용한다.

Eviction 알고리즘과 memory-mapped file write-back에서 이 bit들을 활용한다.

### pml4_for_each

```c
bool pml4_for_each (uint64_t *pml4, pte_for_each_func *func, void *aux);
```

이 함수는 page table을 순회하며 각 page table entry에 대해 callback을 호출한다. SPT를 page table과 동기화하거나 디버깅할 때 유용할 수 있다.

---

## E. Page Table

Pintos-KAIST의 page table은 x86-64의 PML4 기반 구조를 사용한다. 각 process는 자신의 PML4를 가진다.

### 생성, 파괴, 활성화

```c
uint64_t *pml4_create (void);
void pml4_destroy (uint64_t *pml4);
void pml4_activate (uint64_t *pml4);
```

`pml4_create`는 새 page table을 만들고 kernel mapping을 포함하도록 초기화한다.

`pml4_destroy`는 page table에 사용된 메모리를 해제한다. 사용자 page frame을 자동으로 모두 해제한다고 가정하면 안 된다. SPT와 frame table 정리가 먼저 정확히 수행되어야 한다.

`pml4_activate`는 현재 CPU가 사용할 page table을 설정한다. context switch나 process activation에서 호출된다.

### Page Mapping

```c
bool pml4_set_page (uint64_t *pml4, void *upage, void *kpage, bool rw);
void *pml4_get_page (uint64_t *pml4, const void *uaddr);
void pml4_clear_page (uint64_t *pml4, void *upage);
```

`pml4_set_page`는 user page `upage`를 kernel virtual address `kpage`가 가리키는 frame에 매핑한다. `rw`가 true이면 writable mapping을 만든다.

`pml4_get_page`는 user address가 매핑된 kernel virtual address를 반환한다. 매핑이 없으면 `NULL`을 반환한다.

`pml4_clear_page`는 해당 user page의 mapping을 제거한다. eviction이나 page destroy 시 필요하다.

### Accessed/Dirty Bit

```c
bool pml4_is_dirty (uint64_t *pml4, const void *vpage);
void pml4_set_dirty (uint64_t *pml4, const void *vpage, bool dirty);
bool pml4_is_accessed (uint64_t *pml4, const void *vpage);
void pml4_set_accessed (uint64_t *pml4, const void *vpage, bool accessed);
```

Dirty bit는 page가 write되었는지 나타낸다. memory-mapped file을 unmap할 때 dirty page만 파일에 기록하기 위해 사용한다.

Accessed bit는 page가 최근 접근되었는지 나타낸다. clock eviction 알고리즘에서 second chance를 줄 때 사용할 수 있다.

이 bit들은 하드웨어가 자동으로 설정하지만, 커널이 명시적으로 지울 수 있다. 예를 들어 clock 알고리즘에서는 accessed bit가 set된 page를 만나면 bit를 clear하고 다음 후보로 넘어간다.

---

## F. Debugging Tools

Pintos 디버깅은 쉽지 않다. 커널 코드, 사용자 프로그램, page fault, scheduler, file system이 함께 얽히기 때문이다. 문서의 디버깅 도구를 적극적으로 사용하는 것이 중요하다.

### printf

가장 단순한 방법은 `printf`다. thread 이름, fault address, page type, frame address, file offset 등을 출력하면 흐름을 파악하기 쉽다.

다만 timer interrupt나 scheduler와 섞이면 출력 순서가 복잡해질 수 있고, 지나친 출력은 테스트 시간 초과를 만들 수 있다. 디버깅이 끝난 뒤에는 제거해야 한다.

### ASSERT

```c
ASSERT (condition);
```

`ASSERT`는 조건이 false일 때 panic을 발생시킨다. 불변식이 깨지는 지점을 빠르게 찾는 데 유용하다.

예시:

```c
ASSERT (pg_ofs (upage) == 0);
ASSERT (is_user_vaddr (upage));
ASSERT (page->frame == NULL);
```

### 유용한 Attribute

Pintos는 GCC attribute를 감싼 매크로를 제공한다.

- `UNUSED`: 사용하지 않는 parameter나 variable 경고를 줄인다.
- `NO_RETURN`: 함수가 반환하지 않음을 표시한다.
- `NO_INLINE`: 함수를 inline하지 않게 한다. backtrace가 명확해진다.
- `PRINTF_FORMAT`: printf 스타일 format string 검사를 가능하게 한다.

### Backtrace

Pintos에는 backtrace를 출력하고 해석하는 도구가 있다. kernel panic이 발생하면 call stack 주소가 출력된다. 이를 `backtrace` 도구로 해석하면 어떤 함수 경로에서 문제가 발생했는지 알 수 있다.

일반적인 사용은 다음과 같다.

```bash
backtrace kernel.o 0xADDR 0xADDR ...
```

주소는 panic 출력에 나온 call stack 주소를 사용한다.

### GDB 사용

Pintos는 QEMU와 GDB를 연결할 수 있다. 보통 한 터미널에서 Pintos를 GDB 대기 모드로 실행하고, 다른 터미널에서 GDB를 연결한다.

```bash
pintos --gdb -- run test-name
```

GDB에서 사용할 수 있는 명령 예시는 다음과 같다.

```gdb
target remote localhost:1234
break function_name
continue
step
next
print variable
backtrace
info threads
```

Pwndbg 같은 GDB 확장도 도움이 될 수 있다. 단, kernel debugging에서는 일반 user-space debugging과 다른 점이 많으므로, 주소 공간과 symbol이 올바르게 잡혔는지 확인해야 한다.

### Triple Fault 디버깅

Triple fault가 발생하면 CPU가 reset되거나 QEMU가 갑자기 종료될 수 있다. 보통 page table 설정, interrupt handling, kernel stack 손상, 잘못된 pointer 역참조가 원인이다.

문서에서는 binary search 방식으로 무한 루프를 넣어 어디까지 실행되는지 확인하는 방법을 제안한다.

```c
for (;;);
```

이 코드를 의심 지점 사이에 넣어 실행이 멈추는 위치를 좁혀 간다.

### 0xcc 패턴

`0xcc`는 Pintos에서 해제되었거나 초기화되지 않은 메모리에서 자주 보이는 패턴이다. 포인터 값이나 데이터가 `0xcccccccc...`처럼 보이면 초기화 누락, use-after-free, stack overflow 등을 의심해야 한다.

---

## G. Development Tools

### Tags

`ctags` 또는 `etags`를 사용하면 함수와 구조체 정의로 빠르게 이동할 수 있다.

```bash
make tags
```

생성된 tags 파일을 편집기에서 사용하면 `struct thread`, `pml4_set_page`, `vm_try_handle_fault` 같은 정의를 빠르게 찾을 수 있다.

### Cscope

`cscope`는 C 코드베이스 탐색에 유용하다.

```bash
make cscope
cscope -d
```

함수 정의, 호출 위치, 문자열, 파일 등을 검색할 수 있다. Pintos처럼 여러 디렉터리에 코드가 흩어진 프로젝트에서 특히 유용하다.

### vi 종료

문서에는 vi를 처음 쓰는 사람을 위한 기본 안내도 포함되어 있다.

- 저장 후 종료: `:wq`
- 저장하지 않고 종료: `:q!`
- 입력 모드 진입: `i`
- 명령 모드로 돌아가기: `Esc`

---

## H. Hash Table

Pintos의 hash table은 `lib/kernel/hash.h`와 관련 구현에서 제공된다. Project 3의 SPT 구현에 가장 많이 사용된다.

### 기본 구조

```c
struct hash;
struct hash_elem;
```

hash table에 넣을 객체는 내부에 `struct hash_elem` 필드를 포함해야 한다.

예를 들어 page를 hash table에 넣으려면 다음처럼 만들 수 있다.

```c
struct page {
    void *va;
    struct hash_elem elem;
    /* other fields */
};
```

그 다음 `hash_entry` 매크로로 `struct hash_elem *`에서 바깥 구조체 포인터를 얻는다.

```c
struct page *page = hash_entry (elem, struct page, elem);
```

### Hash 함수와 Less 함수

Hash table 초기화에는 hash 함수와 less 함수가 필요하다.

```c
typedef unsigned hash_hash_func (const struct hash_elem *e, void *aux);
typedef bool hash_less_func (const struct hash_elem *a,
                             const struct hash_elem *b,
                             void *aux);
```

SPT에서는 보통 page의 가상 주소를 key로 사용한다.

```c
static unsigned
page_hash (const struct hash_elem *e, void *aux UNUSED) {
    const struct page *p = hash_entry (e, struct page, elem);
    return hash_bytes (&p->va, sizeof p->va);
}

static bool
page_less (const struct hash_elem *a,
           const struct hash_elem *b,
           void *aux UNUSED) {
    const struct page *pa = hash_entry (a, struct page, elem);
    const struct page *pb = hash_entry (b, struct page, elem);
    return pa->va < pb->va;
}
```

key로 사용하는 값은 hash table 안에 들어간 동안 바뀌면 안 된다. 예를 들어 page의 `va`를 삽입 후 변경하면 hash table이 깨진다.

### 초기화와 제거

```c
bool hash_init (struct hash *h,
                hash_hash_func *hash,
                hash_less_func *less,
                void *aux);
void hash_clear (struct hash *h, hash_action_func *action);
void hash_destroy (struct hash *h, hash_action_func *action);
size_t hash_size (struct hash *h);
bool hash_empty (struct hash *h);
```

`hash_init`은 hash table을 초기화한다. `hash_clear`는 모든 element를 제거하고, `hash_destroy`는 table 자체의 내부 자원도 정리한다.

`action` callback을 넘기면 각 element에 대해 추가 정리 작업을 수행할 수 있다. SPT kill에서는 이 callback 안에서 page destroy와 free를 수행하는 방식이 자연스럽다.

### 삽입, 교체, 검색, 삭제

```c
struct hash_elem *hash_insert (struct hash *h, struct hash_elem *new);
struct hash_elem *hash_replace (struct hash *h, struct hash_elem *new);
struct hash_elem *hash_find (struct hash *h, struct hash_elem *e);
struct hash_elem *hash_delete (struct hash *h, struct hash_elem *e);
```

`hash_insert`는 같은 key가 없으면 삽입하고 `NULL`을 반환한다. 같은 key가 이미 있으면 기존 element를 반환하고 새 element는 삽입하지 않는다.

`hash_replace`는 같은 key가 있으면 기존 element를 제거하고 새 element를 넣는다. 기존 element가 없으면 새 element만 삽입한다.

`hash_find`는 같은 key의 element를 찾는다. 검색용 임시 객체를 만들어 key 필드만 채운 뒤 사용할 수 있다.

`hash_delete`는 element를 제거하고 제거된 element를 반환한다.

### 순회

```c
void hash_apply (struct hash *h, hash_action_func *action);
```

`hash_apply`는 모든 element에 callback을 적용한다. 단, 순회 중 table 구조를 변경하는 작업은 주의해야 한다.

Iterator도 제공된다.

```c
struct hash_iterator i;
hash_first (&i, &hash);
while (hash_next (&i)) {
    struct page *p = hash_entry (hash_cur (&i), struct page, elem);
    /* use p */
}
```

### SPT 구현 예시 흐름

SPT를 hash table로 구현하면 다음 흐름이 된다.

```c
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
    hash_init (&spt->pages, page_hash, page_less, NULL);
}

struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
    struct page temp;
    temp.va = pg_round_down (va);
    struct hash_elem *e = hash_find (&spt->pages, &temp.elem);
    return e == NULL ? NULL : hash_entry (e, struct page, elem);
}

bool
spt_insert_page (struct supplemental_page_table *spt, struct page *page) {
    return hash_insert (&spt->pages, &page->elem) == NULL;
}
```

검색용 임시 객체를 사용할 때는 `page_less`와 `page_hash`가 `va`만 참조해야 한다. 다른 초기화되지 않은 필드를 읽으면 안 된다.

### aux 인자

Hash 함수, less 함수, action 함수에는 `void *aux`가 전달된다. 보통 필요 없으면 `NULL`을 넘기고 `UNUSED`로 표시한다. 필요한 경우 SPT copy나 destroy 과정에서 추가 context를 전달하는 데 사용할 수 있다.

### 동기화

Pintos hash table 자체는 thread-safe가 아니다. 여러 thread가 같은 hash table에 접근할 수 있다면 caller가 lock으로 보호해야 한다. 프로세스별 SPT는 일반적으로 해당 프로세스 context에서 접근되지만, fork, exit, page fault, file operation이 겹칠 수 있으므로 설계에 따라 lock이 필요할 수 있다.

---

# 구현 체크리스트

이 번역본의 내용을 실제 구현으로 옮길 때는 다음 순서가 비교적 안전하다.

1. SPT를 hash table로 구현한다.
2. `vm_get_frame`, `vm_claim_page`, `vm_do_claim_page`를 구현한다.
3. executable segment를 lazy loading으로 바꾼다.
4. anonymous page initializer와 lazy loader를 완성한다.
5. stack setup과 stack growth를 구현한다.
6. SPT copy/kill을 구현해 fork와 exit를 안정화한다.
7. mmap/munmap과 file-backed page를 구현한다.
8. frame table과 eviction을 구현한다.
9. anonymous swap in/out을 구현한다.
10. file-backed page eviction과 dirty write-back을 마무리한다.
11. extra를 선택했다면 COW를 구현한다.

---

# 자주 틀리는 부분 요약

- `PAL_USER` 없이 frame을 할당하는 실수.
- SPT 검색 시 `pg_round_down`을 하지 않는 실수.
- lazy loading aux를 stack 지역 변수로 넘기는 실수.
- `file_reopen` 없이 원래 fd의 file 객체를 mmap page가 공유하는 실수.
- dirty page의 write-back 범위를 PGSIZE 전체로 잡아 파일 끝을 잘못 확장하는 실수.
- page table mapping 제거 없이 frame만 해제하는 실수.
- kernel mode page fault에서 `f->rsp`를 user rsp로 착각하는 실수.
- process exit에서 mmap page를 write-back하지 않는 실수.
- fork에서 uninit/file/anon page별 복사 처리를 구분하지 않는 실수.
- frame table과 swap table에 lock을 두지 않아 race condition이 발생하는 실수.

---

# 원문 우선 원칙

이 문서는 학습을 돕기 위한 한국어 번역/정리본이다. 함수 이름, 구조체 이름, 테스트 요구사항, 채점 기준은 원문과 Pintos-KAIST 코드가 최종 기준이다. 구현 중 애매한 부분이 있으면 원문 문서와 실제 skeleton code를 우선 확인해야 한다.
