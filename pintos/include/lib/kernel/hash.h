#ifndef __LIB_KERNEL_HASH_H
#define __LIB_KERNEL_HASH_H

/* 해시 테이블.
 *
 * 이 자료구조는 Project 3용 Tour of Pintos에 자세히 문서화되어 있다.
 *
 * 이것은 chaining을 사용하는 표준 해시 테이블이다.
 * 테이블에서 원소를 찾기 위해 원소 데이터에 hash function을 계산하고,
 * 그 값을 이중 연결 리스트 배열의 index로 사용한 뒤 리스트를 선형 탐색한다.
 *
 * chain 리스트는 동적 할당을 사용하지 않는다.
 * 대신 hash에 들어갈 수 있는 각 구조체는 struct hash_elem 멤버를 포함해야 한다.
 * 모든 hash 함수는 이런 `struct hash_elem'을 대상으로 동작한다.
 * hash_entry 매크로는 struct hash_elem에서 그것을 포함하는 구조체 객체로
 * 다시 변환하게 해 준다. 이는 연결 리스트 구현에서 사용하는 것과 같은 기법이다.
 * 자세한 설명은 lib/kernel/list.h를 참고하라. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "list.h"

/* 해시 원소. */
struct hash_elem {
	struct list_elem list_elem;
};

/* 해시 원소 HASH_ELEM을 가리키는 pointer를,
 * HASH_ELEM을 포함하는 구조체에 대한 pointer로 변환한다.
 * 바깥 구조체 이름 STRUCT와 해시 원소 멤버 이름 MEMBER를 넘겨라.
 * 예시는 파일 상단의 큰 주석을 참고하라. */
#define hash_entry(HASH_ELEM, STRUCT, MEMBER)                   \
	((STRUCT *) ((uint8_t *) &(HASH_ELEM)->list_elem        \
		- offsetof (STRUCT, MEMBER.list_elem)))

/* 보조 데이터 AUX가 주어졌을 때 해시 원소 E의 hash 값을 계산해 반환한다. */
typedef uint64_t hash_hash_func (const struct hash_elem *e, void *aux);

/* 보조 데이터 AUX가 주어졌을 때 두 해시 원소 A와 B의 값을 비교한다.
 * A가 B보다 작으면 true를, A가 B보다 크거나 같으면 false를 반환한다. */
typedef bool hash_less_func (const struct hash_elem *a,
		const struct hash_elem *b,
		void *aux);

/* 보조 데이터 AUX가 주어졌을 때 해시 원소 E에 어떤 연산을 수행한다. */
typedef void hash_action_func (struct hash_elem *e, void *aux);

/* 해시 테이블. */
struct hash {
	size_t elem_cnt;            /* 테이블의 원소 수. */
	size_t bucket_cnt;          /* bucket 수, 2의 거듭제곱. */
	struct list *buckets;       /* `bucket_cnt'개 리스트의 배열. */
	hash_hash_func *hash;       /* hash 함수. */
	hash_less_func *less;       /* 비교 함수. */
	void *aux;                  /* `hash'와 `less'용 보조 데이터. */
};

/* 해시 테이블 iterator. */
struct hash_iterator {
	struct hash *hash;          /* 해시 테이블. */
	struct list *bucket;        /* 현재 bucket. */
	struct hash_elem *elem;     /* 현재 bucket의 현재 해시 원소. */
};

/* 기본 생명 주기. */
bool hash_init (struct hash *, hash_hash_func *, hash_less_func *, void *aux);
void hash_clear (struct hash *, hash_action_func *);
void hash_destroy (struct hash *, hash_action_func *);

/* 탐색, 삽입, 삭제. */
struct hash_elem *hash_insert (struct hash *, struct hash_elem *);
struct hash_elem *hash_replace (struct hash *, struct hash_elem *);
struct hash_elem *hash_find (struct hash *, struct hash_elem *);
struct hash_elem *hash_delete (struct hash *, struct hash_elem *);

/* 순회. */
void hash_apply (struct hash *, hash_action_func *);
void hash_first (struct hash_iterator *, struct hash *);
struct hash_elem *hash_next (struct hash_iterator *);
struct hash_elem *hash_cur (struct hash_iterator *);

/* 정보. */
size_t hash_size (struct hash *);
bool hash_empty (struct hash *);

/* 예시 hash 함수들. */
uint64_t hash_bytes (const void *, size_t);
uint64_t hash_string (const char *);
uint64_t hash_int (int);

#endif /* lib/kernel/hash.h */
