#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H

/* 이중 연결 리스트.
 *
 * 이 이중 연결 리스트 구현은 동적 할당 메모리를 필요로 하지 않는다.
 * 대신 리스트 원소가 될 가능성이 있는 각 구조체는 struct list_elem 멤버를
 * 포함해야 한다. 모든 리스트 함수는 이런 `struct list_elem'을 대상으로
 * 동작한다. list_entry 매크로는 struct list_elem에서 그것을 포함하는
 * 구조체 객체로 다시 변환하게 해 준다.
 * 예를 들어 `struct foo'의 리스트가 필요하다고 하자.
 * `struct foo'는 아래처럼 `struct list_elem' 멤버를 가져야 한다:
 * struct foo {
 *   struct list_elem elem;
 *   int bar;
 *   ...other members...
 * };
 * 그러면 `struct foo'의 리스트를 아래처럼 선언하고 초기화할 수 있다:
 * struct list foo_list;
 * list_init (&foo_list);
 * iteration은 struct list_elem에서 그것을 둘러싼 구조체로 다시 변환해야 하는
 * 전형적인 상황이다. foo_list를 사용한 예시는 다음과 같다:
 * struct list_elem *e;
 * for (e = list_begin (&foo_list); e != list_end (&foo_list);
 * e = list_next (e)) {
 *   struct foo *f = list_entry (e, struct foo, elem);
 *   ...do something with f...
 * }
 * 실제 리스트 사용 예는 소스 전체에서 찾을 수 있다.
 * 예를 들어 threads 디렉터리의 malloc.c, palloc.c, thread.c는 모두
 * 리스트를 사용한다.
 * 이 리스트의 인터페이스는 C++ STL의 list<> 템플릿에서 영감을 받았다.
 * list<>에 익숙하다면 쉽게 사용할 수 있을 것이다. 다만 강조하자면,
 * 이 리스트는 타입 검사를 *전혀* 하지 않으며 그 밖의 정확성 검사도 거의 하지 못한다.
 * 잘못 사용하면 곧바로 문제가 생긴다.
 * 리스트 용어집:
 * - "front"(앞쪽): 리스트의 첫 번째 원소.
 * empty list에서는 정의되지 않는다. list_front()가 반환한다.
 * - "back"(뒤쪽): 리스트의 마지막 원소.
 * empty list에서는 정의되지 않는다. list_back()이 반환한다.
 * - "tail"(꼬리): 비유적으로 리스트의 마지막 원소 바로 뒤에 있는 원소.
 * empty list에서도 잘 정의된다.
 * list_end()가 반환한다. front에서 back으로 가는 iteration의
 * 끝 sentinel로 사용된다.
 * - "beginning"(시작점): non-empty list에서는 front, empty list에서는 tail이다.
 * list_begin()이 반환한다. front에서 back으로 가는 iteration의
 * 시작점으로 사용된다.
 * - "head"(머리): 비유적으로 리스트의 첫 번째 원소 바로 앞에 있는 원소.
 * empty list에서도 잘 정의된다.
 * list_rend()가 반환한다. back에서 front로 가는 iteration의
 * 끝 sentinel로 사용된다.
 * - "reverse beginning"(역방향 시작점): non-empty list에서는 back,
 * empty list에서는 head다. list_rbegin()이 반환한다.
 * back에서 front로 가는 iteration의 시작점으로 사용된다.
 *
 * - "interior element"(내부 원소): head나 tail이 아닌 원소,
 * 즉 실제 리스트 원소를 뜻한다. empty list에는 내부 원소가 없다.*/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 리스트 원소. */
struct list_elem {
	struct list_elem *prev;     /* 이전 리스트 원소. */
	struct list_elem *next;     /* 다음 리스트 원소. */
};

/* 리스트. */
struct list {
	struct list_elem head;      /* 리스트 head. */
	struct list_elem tail;      /* 리스트 tail. */
};

/* 리스트 원소 LIST_ELEM을 가리키는 pointer를,
   LIST_ELEM을 포함하는 구조체에 대한 pointer로 변환한다.
   바깥 구조체 이름 STRUCT와 list element 멤버 이름 MEMBER를 넘겨라.
   예시는 파일 상단의 큰 주석을 참고하라. */
#define list_entry(LIST_ELEM, STRUCT, MEMBER)           \
	((STRUCT *) ((uint8_t *) &(LIST_ELEM)->next     \
		- offsetof (STRUCT, MEMBER.next)))

void list_init (struct list *);

/* 리스트 순회. */
struct list_elem *list_begin (struct list *);
struct list_elem *list_next (struct list_elem *);
struct list_elem *list_end (struct list *);

struct list_elem *list_rbegin (struct list *);
struct list_elem *list_prev (struct list_elem *);
struct list_elem *list_rend (struct list *);

struct list_elem *list_head (struct list *);
struct list_elem *list_tail (struct list *);

/* 리스트 삽입. */
void list_insert (struct list_elem *, struct list_elem *);
void list_splice (struct list_elem *before,
		struct list_elem *first, struct list_elem *last);
void list_push_front (struct list *, struct list_elem *);
void list_push_back (struct list *, struct list_elem *);

/* 리스트 제거. */
struct list_elem *list_remove (struct list_elem *);
struct list_elem *list_pop_front (struct list *);
struct list_elem *list_pop_back (struct list *);

/* 리스트 원소들. */
struct list_elem *list_front (struct list *);
struct list_elem *list_back (struct list *);

/* 리스트 속성. */
size_t list_size (struct list *);
bool list_empty (struct list *);

/* 기타. */
void list_reverse (struct list *);

/* 보조 데이터 AUX가 주어졌을 때 두 리스트 원소 A와 B의 값을 비교한다.
   A가 B보다 작으면 true를, A가 B보다 크거나 같으면 false를 반환한다. */
typedef bool list_less_func (const struct list_elem *a,
                             const struct list_elem *b,
                             void *aux);

/* 정렬된 원소를 가진 리스트에 대한 연산. */
void list_sort (struct list *,
                list_less_func *, void *aux);
void list_insert_ordered (struct list *, struct list_elem *,
                          list_less_func *, void *aux);
void list_unique (struct list *, struct list *duplicates,
                  list_less_func *, void *aux);

/* 최댓값과 최솟값. */
struct list_elem *list_max (struct list *, list_less_func *, void *aux);
struct list_elem *list_min (struct list *, list_less_func *, void *aux);

#endif /* lib/kernel/list.h */
