#include "random.h"
#include <stdbool.h>
#include <stdint.h>
#include "debug.h"

/* RC4 기반 의사 난수 생성기(PRNG).

   RC4는 스트림 암호입니다. 여기서는 암호학적 특성 때문에 사용하는 것이 아니라, 구현이 쉽고 비암호학적 용도에는 출력이 충분히
   무작위이기 때문입니다.

   RC4에 대한 정보는 http://en.wikipedia.org/wiki/RC4_(cipher)를 참조하세요. */

/* RC4 상태. */
static uint8_t s[256];          /* S[]. */
static uint8_t s_i, s_j;        /* i, j. */

/* 이미 초기화되었습니까? */
static bool inited;     

/* A와 B가 가리키는 바이트를 서로 교환합니다. */
static inline void
swap_byte (uint8_t *a, uint8_t *b) {
	uint8_t t = *a;
	*a = *b;
	*b = t;
}

/* 주어진 SEED로 PRNG를 초기화하거나 재초기화합니다. */
void
random_init (unsigned seed) {
	uint8_t *seedp = (uint8_t *) &seed;
	int i;
	uint8_t j;

	for (i = 0; i < 256; i++) 
		s[i] = i;
	for (i = j = 0; i < 256; i++) {
		j += s[i] + seedp[i % sizeof seed];
		swap_byte (s + i, s + j);
	}

	s_i = s_j = 0;
	inited = true;
}

/* SIZE개의 무작위 바이트를 BUF에 씁니다. */
void
random_bytes (void *buf_, size_t size) {
	uint8_t *buf;

	if (!inited)
		random_init (0);

	for (buf = buf_; size-- > 0; buf++) {
		uint8_t s_k;

		s_i++;
		s_j += s[s_i];
		swap_byte (s + s_i, s + s_j);

		s_k = s[s_i] + s[s_j];
		*buf = s[s_k];
	}
}

/* 의사 난수 unsigned long을 반환합니다.
   random_ulong() % n을 사용하면 0...n(exclusive) 범위의 난수를 얻을 수 있습니다. */
unsigned long
random_ulong (void) {
	unsigned long ul;
	random_bytes (&ul, sizeof ul);
	return ul;
}
