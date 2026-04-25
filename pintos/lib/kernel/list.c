#include "list.h"
#include "../debug.h"

/* 우리의 이중 연결 리스트에는 두 개의 헤더 요소가 있습니다: 첫 번째 요소 바로 앞의 "head"와 마지막 요소 바로 뒤의
   "tail"입니다. 앞쪽 헤더의 `prev` 링크는 null이고, 뒤쪽 헤더의 `next` 링크도 null입니다. 나머지 두 링크는
   리스트의 내부 요소들을 통해 서로를 가리킵니다.

   빈 리스트는 다음과 같습니다:

   +------+     +------+
   <---| head |<--->| tail |--->
   +------+     +------+

   두 요소가 있는 리스트는 다음과 같습니다:

   +------+     +-------+     +-------+     +------+
   <---| head |<--->|   1   |<--->|   2   |<--->| tail |<--->
   +------+     +-------+     +-------+     +------+

   이러한 배열의 대칭성 덕분에 리스트 처리에서 특수한 경우가 많이 사라집니다. 예를 들어 list_remove()를 보세요: 포인터 대입
   두 번만으로 끝나고 조건문도 없습니다. 헤더 요소가 없었다면 코드는 훨씬 더 복잡했을 것입니다.

   (각 헤더 요소에서 포인터 하나만 사용되므로, 사실은 둘을 하나의 헤더 요소로 합쳐도 이 단순성을 잃지 않습니다. 하지만 두 개의 별도
   요소를 사용하면 일부 연산에서 조금 더 검사를 할 수 있고, 이는 유용할 수 있습니다.) */

static bool is_sorted (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) UNUSED;

/* ELEM이 head이면 true를 반환하고, 그렇지 않으면 false를 반환합니다. */
static inline bool
is_head (struct list_elem *elem) {
	return elem != NULL && elem->prev == NULL && elem->next != NULL;
}

/* ELEM이 내부 요소이면 true를 반환하고, 그렇지 않으면 false를 반환합니다. */
static inline bool
is_interior (struct list_elem *elem) {
	return elem != NULL && elem->prev != NULL && elem->next != NULL;
}

/* ELEM이 tail이면 true를 반환하고, 그렇지 않으면 false를 반환합니다. */
static inline bool
is_tail (struct list_elem *elem) {
	return elem != NULL && elem->prev != NULL && elem->next == NULL;
}

/* LIST를 빈 리스트로 초기화합니다. */
void
list_init (struct list *list) {
	ASSERT (list != NULL);
	list->head.prev = NULL;
	list->head.next = &list->tail;
	list->tail.prev = &list->head;
	list->tail.next = NULL;
}

/* LIST의 시작을 반환합니다. */
struct list_elem *
list_begin (struct list *list) {
	ASSERT (list != NULL);
	return list->head.next;
}

/* ELEM의 리스트에서 ELEM 다음 요소를 반환합니다. ELEM이 그 리스트의 마지막 요소이면 list tail을 반환합니다. ELEM
   자체가 list tail인 경우 결과는 정의되지 않습니다. */
struct list_elem *
list_next (struct list_elem *elem) {
	ASSERT (is_head (elem) || is_interior (elem));
	return elem->next;
}

/* LIST의 tail을 반환합니다.

   list_end()는 리스트를 앞에서 뒤로 순회할 때 자주 사용됩니다. list.h 상단의 큰 주석을 보세요. */
struct list_elem *
list_end (struct list *list) {
	ASSERT (list != NULL);
	return &list->tail;
}

/* LIST의 역방향 시작점을 반환하며, LIST를 뒤에서 앞으로 역순으로 순회할 때 사용합니다. */
struct list_elem *
list_rbegin (struct list *list) {
	ASSERT (list != NULL);
	return list->tail.prev;
}

/* ELEM의 리스트에서 ELEM 이전 요소를 반환합니다. ELEM이 그 리스트의 첫 번째 요소이면 list head를 반환합니다.
   ELEM 자체가 list head인 경우 결과는 정의되지 않습니다. */
struct list_elem *
list_prev (struct list_elem *elem) {
	ASSERT (is_interior (elem) || is_tail (elem));
	return elem->prev;
}

/* LIST의 head를 반환합니다.
 *
 * list_rend()는 리스트를 뒤에서 앞으로 역순으로 순회할 때 자주 사용됩니다. list.h 상단의 예제를 따른 일반적인 사용 예는
 * 다음과 같습니다:
 *
 * for (e = list_rbegin (&foo_list); e != list_rend (&foo_list);
 * e = list_prev (e))
 * {
 * struct foo *f = list_entry (e, struct foo, elem);
 * ...do something with f...
 * } */
struct list_elem *
list_rend (struct list *list) {
	ASSERT (list != NULL);
	return &list->head;
}

/* LIST의 head를 반환합니다.
 *
 * list_head()는 리스트를 순회하는 대체 방식으로 사용할 수 있습니다. 예:
 *
 * e = list_head (&list);
 * while ((e = list_next (e)) != list_end (&list))
 * {
 * ...
 * } */
struct list_elem *
list_head (struct list *list) {
	ASSERT (list != NULL);
	return &list->head;
}

/* LIST의 tail을 반환합니다. */
struct list_elem *
list_tail (struct list *list) {
	ASSERT (list != NULL);
	return &list->tail;
}

/* ELEM을 BEFORE 바로 앞에 삽입합니다. BEFORE는 내부 요소이거나 tail일 수 있습니다. 후자의 경우는
   list_push_back()과 같습니다. */
void
list_insert (struct list_elem *before, struct list_elem *elem) {
	ASSERT (is_interior (before) || is_tail (before));
	ASSERT (elem != NULL);

	elem->prev = before->prev;
	elem->next = before;
	before->prev->next = elem;
	before->prev = elem;
}

/* FIRST부터 LAST 직전(exclusive)까지의 요소를 현재 리스트에서 제거한 뒤, BEFORE 바로 앞에 삽입합니다.
   BEFORE는 내부 요소이거나 tail일 수 있습니다. */
void
list_splice (struct list_elem *before,
		struct list_elem *first, struct list_elem *last) {
	ASSERT (is_interior (before) || is_tail (before));
	if (first == last)
		return;
	last = list_prev (last);

	ASSERT (is_interior (first));
	ASSERT (is_interior (last));

	/* FIRST...LAST를 현재 리스트에서 깔끔하게 제거합니다. */
	first->prev->next = last->next;
	last->next->prev = first->prev;

	/* FIRST...LAST를 새 리스트에 스플라이스합니다. */
	first->prev = before->prev;
	last->next = before;
	before->prev->next = first;
	before->prev = last;
}

/* ELEM을 LIST의 시작 부분에 삽입하여 LIST의 맨 앞이 되게 합니다. */
void
list_push_front (struct list *list, struct list_elem *elem) {
	list_insert (list_begin (list), elem);
}

/* ELEM을 LIST의 끝에 삽입하여 LIST의 맨 뒤가 되게 합니다. */
void
list_push_back (struct list *list, struct list_elem *elem) {
	list_insert (list_end (list), elem);
}

/* ELEM을 리스트에서 제거하고 그 다음 요소를 반환합니다. ELEM이 리스트에 없으면 동작은 정의되지 않습니다.
 *
 * 제거한 뒤에는 ELEM을 리스트의 요소로 취급하면 안전하지 않습니다. 특히 제거 후 ELEM에 대해 list_next()나
 * list_prev()를 사용하면 정의되지 않은 동작이 발생합니다. 즉, 리스트의 요소를 제거하는 단순한 루프는 실패합니다:
 *
 * * DON'T DO THIS **
 * for (e = list_begin (&list); e != list_end (&list); e = list_next (e))
 * {
 * ...do something with e...
 * list_remove (e);
 * }
 * * DON'T DO THIS **
 *
 * 다음은 리스트를 순회하면서 요소를 제거하는 한 가지 올바른 방법입니다:
 *
 * for (e = list_begin (&list); e != list_end (&list); e = list_remove (e))
 * {
 * ...do something with e...
 * }
 *
 * 리스트의 요소를 free()해야 한다면 더 보수적으로 접근해야 합니다. 그런 경우에도 동작하는 대안 전략은 다음과 같습니다:
 *
 * while (!list_empty (&list))
 * {
 * struct list_elem *e = list_pop_front (&list);
 * ...do something with e...
 * } */
struct list_elem *
list_remove (struct list_elem *elem) {
	ASSERT (is_interior (elem));
	elem->prev->next = elem->next;
	elem->next->prev = elem->prev;
	return elem->next;
}

/* LIST에서 맨 앞 요소를 제거하고 반환합니다. 제거 전에 LIST가 비어 있으면 동작은 정의되지 않습니다. */
struct list_elem *
list_pop_front (struct list *list) {
	struct list_elem *front = list_front (list);
	list_remove (front);
	return front;
}

/* LIST에서 맨 뒤 요소를 제거하고 반환합니다. 제거 전에 LIST가 비어 있으면 동작은 정의되지 않습니다. */
struct list_elem *
list_pop_back (struct list *list) {
	struct list_elem *back = list_back (list);
	list_remove (back);
	return back;
}

/* LIST의 맨 앞 요소를 반환합니다. LIST가 비어 있으면 동작은 정의되지 않습니다. */
struct list_elem *
list_front (struct list *list) {
	ASSERT (!list_empty (list));
	return list->head.next;
}

/* LIST의 맨 뒤 요소를 반환합니다. LIST가 비어 있으면 동작은 정의되지 않습니다. */
struct list_elem *
list_back (struct list *list) {
	ASSERT (!list_empty (list));
	return list->tail.prev;
}

/* LIST의 요소 개수를 반환합니다. 요소 수에 대해 O(n) 시간에 실행됩니다. */
size_t
list_size (struct list *list) {
	struct list_elem *e;
	size_t cnt = 0;

	for (e = list_begin (list); e != list_end (list); e = list_next (e))
		cnt++;
	return cnt;
}

/* LIST가 비어 있으면 true를 반환하고, 그렇지 않으면 false를 반환합니다. */
bool
list_empty (struct list *list) {
	return list_begin (list) == list_end (list);
}

/* A와 B가 가리키는 `struct list_elem *`를 서로 바꿉니다. */
static void
swap (struct list_elem **a, struct list_elem **b) {
	struct list_elem *t = *a;
	*a = *b;
	*b = t;
}

/* LIST의 순서를 뒤집습니다. */
void
list_reverse (struct list *list) {
	if (!list_empty (list)) {
		struct list_elem *e;

		for (e = list_begin (list); e != list_end (list); e = e->prev)
			swap (&e->prev, &e->next);
		swap (&list->head.next, &list->tail.prev);
		swap (&list->head.next->prev, &list->tail.prev->next);
	}
}

/* 보조 데이터 AUX와 함께 LESS에 따라 A부터 B 직전(exclusive)까지의 리스트 요소가 순서대로 있을 때만 true를
   반환합니다. */
static bool
is_sorted (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) {
	if (a != b)
		while ((a = list_next (a)) != b)
			if (less (a, list_prev (a), aux))
				return false;
	return true;
}

/* 보조 데이터 AUX와 함께 LESS에 따라 비감소 순서인 리스트 요소들의 연속 구간을 찾습니다. 구간은 A에서 시작해 B를 넘지 않는
   지점에서 끝납니다. 해당 run의 (exclusive) 끝을 반환합니다.
   A부터 B 직전(exclusive)까지는 비어 있지 않은 범위여야 합니다. */
static struct list_elem *
find_end_of_run (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) {
	ASSERT (a != NULL);
	ASSERT (b != NULL);
	ASSERT (less != NULL);
	ASSERT (a != b);

	do {
		a = list_next (a);
	} while (a != b && !less (a, list_prev (a), aux));
	return a;
}

/* A0부터 A1B0 직전(exclusive)까지와 A1B0부터 B1 직전(exclusive)까지를 병합하여, B1
   직전(exclusive)에서 끝나는 결합된 범위를 만듭니다. 두 입력 범위는 모두 비어 있지 않아야 하며, 보조 데이터 AUX와 함께
   LESS에 따라 비감소 순서로 정렬되어 있어야 합니다. 출력 범위는 같은 방식으로 정렬됩니다. */
static void
inplace_merge (struct list_elem *a0, struct list_elem *a1b0,
		struct list_elem *b1,
		list_less_func *less, void *aux) {
	ASSERT (a0 != NULL);
	ASSERT (a1b0 != NULL);
	ASSERT (b1 != NULL);
	ASSERT (less != NULL);
	ASSERT (is_sorted (a0, a1b0, less, aux));
	ASSERT (is_sorted (a1b0, b1, less, aux));

	while (a0 != a1b0 && a1b0 != b1)
		if (!less (a1b0, a0, aux))
			a0 = list_next (a0);
		else {
			a1b0 = list_next (a1b0);
			list_splice (a0, list_prev (a1b0), a1b0);
		}
}

/* 보조 데이터 AUX와 함께 LESS에 따라 LIST를 정렬합니다. 요소 수에 대해 O(n lg n) 시간과 O(1) 공간을 사용하는
   자연스러운 반복 병합 정렬을 사용합니다. */
void
list_sort (struct list *list, list_less_func *less, void *aux) {
	size_t output_run_cnt;        /* 현재 패스에서 출력되는 run의 수. */

	ASSERT (list != NULL);
	ASSERT (less != NULL);

	/* 비감소 요소의 인접한 run들을 병합하면서 리스트를 반복적으로 순회하여, run이 하나만 남을 때까지 진행합니다. */
	do {
		struct list_elem *a0;     /* 첫 번째 run의 시작. */
		struct list_elem *a1b0;   /* 첫 번째 run의 끝, 두 번째 run의 시작. */
		struct list_elem *b1;     /* 두 번째 run의 끝. */

		output_run_cnt = 0;
		for (a0 = list_begin (list); a0 != list_end (list); a0 = b1) {
			/* 각 반복은 출력 run 하나를 생성합니다. */
			output_run_cnt++;

			/* 비감소 요소의 인접한 두 run A0...A1B0와 A1B0...B1를 찾습니다. */
			a1b0 = find_end_of_run (a0, list_end (list), less, aux);
			if (a1b0 == list_end (list))
				break;
			b1 = find_end_of_run (a1b0, list_end (list), less, aux);

			/* run들을 병합합니다. */
			inplace_merge (a0, a1b0, b1, less, aux);
		}
	}
	while (output_run_cnt > 1);

	ASSERT (is_sorted (list_begin (list), list_end (list), less, aux));
}

/* ELEM을 LIST의 적절한 위치에 삽입합니다. LIST는 보조 데이터 AUX와 함께 LESS에 따라 정렬되어 있어야 합니다.
   LIST의 요소 수에 대해 평균적으로 O(n)에 실행됩니다. */
void
list_insert_ordered (struct list *list, struct list_elem *elem,
		list_less_func *less, void *aux) {
	struct list_elem *e;

	ASSERT (list != NULL);
	ASSERT (elem != NULL);
	ASSERT (less != NULL);

	for (e = list_begin (list); e != list_end (list); e = list_next (e))
		if (less (elem, e, aux))
			break;
	return list_insert (e, elem);
}

/* LIST를 순회하면서, 보조 데이터 AUX와 함께 LESS에 따라 인접한 요소 집합마다 첫 번째를 제외한 나머지를 모두 제거합니다.
   DUPLICATES가 non-null이면 LIST의 요소들이 DUPLICATES에 추가됩니다. */
void
list_unique (struct list *list, struct list *duplicates,
		list_less_func *less, void *aux) {
	struct list_elem *elem, *next;

	ASSERT (list != NULL);
	ASSERT (less != NULL);
	if (list_empty (list))
		return;

	elem = list_begin (list);
	while ((next = list_next (elem)) != list_end (list))
		if (!less (elem, next, aux) && !less (next, elem, aux)) {
			list_remove (next);
			if (duplicates != NULL)
				list_push_back (duplicates, next);
		} else
			elem = next;
}

/* 보조 데이터 AUX와 함께 LESS에 따라 값이 가장 큰 LIST의 요소를 반환합니다. 최대값이 여러 개면 리스트에서 더 앞에 나오는
   요소를 반환합니다. 리스트가 비어 있으면 tail을 반환합니다. */
struct list_elem *
list_max (struct list *list, list_less_func *less, void *aux) {
	struct list_elem *max = list_begin (list);
	if (max != list_end (list)) {
		struct list_elem *e;

		for (e = list_next (max); e != list_end (list); e = list_next (e))
			if (less (max, e, aux))
				max = e;
	}
	return max;
}

/* 보조 데이터 AUX와 함께 LESS에 따라 값이 가장 작은 LIST의 요소를 반환합니다. 최소값이 여러 개면 리스트에서 더 앞에 나오는
   요소를 반환합니다. 리스트가 비어 있으면 tail을 반환합니다. */
struct list_elem *
list_min (struct list *list, list_less_func *less, void *aux) {
	struct list_elem *min = list_begin (list);
	if (min != list_end (list)) {
		struct list_elem *e;

		for (e = list_next (min); e != list_end (list); e = list_next (e))
			if (less (e, min, aux))
				min = e;
	}
	return min;
}
