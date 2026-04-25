#include <stdint.h>

/* x86에서는 한 64비트 정수를 다른 64비트 정수로 나누는 연산을 단일 명령어나 짧은 명령 시퀀스로는 수행할 수 없다. 따라서
   GCC는 64비트 나눗셈과 나머지 연산을 함수 호출로 구현한다. 이러한 함수들은 보통 libgcc에서 가져오며, libgcc는 GCC가
   수행하는 모든 링크에 자동으로 포함된다.

   하지만 일부 x86-64 머신은 libgcc를 포함한 필요한 라이브러리 없이 32비트 x86 코드를 생성할 수 있는 컴파일러와
   유틸리티를 갖추고 있다. 따라서 우리 자신의 64비트 나눗셈 루틴을 구현하기만 하면 이러한 머신에서도 Pintos를 동작시킬 수
   있으며, Pintos가 필요로 하는 libgcc 루틴은 이것들뿐이다.

   완전성도 이러한 루틴을 포함해야 하는 또 다른 이유다. Pintos가 완전히 자기완결적이라면, 그만큼 덜 수수께끼처럼 느껴진다. */

/* x86 DIVL 명령을 사용해 64비트 N을 32비트 D로 나누어 32비트 몫을 얻는다. 몫을 반환한다.
   몫이 32비트에 들어가지 않으면 나눗셈 오류(#DE)로 트랩한다. */
static inline uint32_t
divl (uint64_t n, uint32_t d) {
	uint32_t n1 = n >> 32;
	uint32_t n0 = n;
	uint32_t q, r;

	asm ("divl %4"
			: "=d" (r), "=a" (q)
			: "0" (n1), "1" (n0), "rm" (d));

	return q;
}

/* X의 선행 0 비트 수를 반환한다.
   X는 0이 아니어야 한다. */
static int
nlz (uint32_t x) {
	/* 이 기법은 이식 가능하지만, 특정 시스템에서는 더 나은 방법이 있다. 충분히 새로운 GCC에서는
	   __builtin_clz()를 사용해 GCC가 이를 어떻게 수행하는지에 대한 지식을 활용할 수 있다. 또는 x86 BSR
	   명령을 직접 사용할 수도 있다. */
	int n = 0;
	if (x <= 0x0000FFFF) {
		n += 16;
		x <<= 16;
	}
	if (x <= 0x00FFFFFF) {
		n += 8;
		x <<= 8;
	}
	if (x <= 0x0FFFFFFF) {
		n += 4;
		x <<= 4;
	}
	if (x <= 0x3FFFFFFF) {
		n += 2;
		x <<= 2;
	}
	if (x <= 0x7FFFFFFF)
		n++;
	return n;
}

/* 부호 없는 64비트 N을 부호 없는 64비트 D로 나누고 몫을 반환한다. */
static uint64_t
udiv64 (uint64_t n, uint64_t d) {
	if ((d >> 32) == 0) {
		/* 정당성 증명:

		   이 함수에서 정의된 대로 n, d, b, n1, n0를 두자.
		   [x]를 x의 "floor"라고 하자. T = b[n1/d]라고 두자. d가
		   0이 아니라고 가정하자. 그러면:
		   [n/d] = [n/d] - T + T
		   = [n/d - T] + T                         아래 (1)에 의해
		   = [(b*n1 + n0)/d - T] + T               n의 정의에 의해
		   = [(b*n1 + n0)/d - dT/d] + T
		   = [(b(n1 - d[n1/d]) + n0)/d] + T
		   = [(b[n1 % d] + n0)/d] + T,             %의 정의에 의해
		   이는 아래에서 계산되는 식이다.

		   (1) 임의의 실수 x, 정수 i에 대해 [x] + i = [x + i]임에 주의하라.

		   divl()이 트랩하지 않도록 하려면, [(b[n1 % d] + n0)/d]는
		   b보다 작아야 한다. [n1 % d]와 n0가 각각 d - 1과 b - 1의
		   최대값을 취한다고 가정하면:
		   [(b(d - 1) + (b - 1))/d] < b
		   <=> [(bd - 1)/d] < b
		   <=> [b - 1/d] < b
		   이는 자명한 항진명제이다.

		   따라서 이 코드는 올바르며 트랩하지 않는다. */
		uint64_t b = 1ULL << 32;
		uint32_t n1 = n >> 32;
		uint32_t n0 = n;
		uint32_t d0 = d;

		return divl (b * (n1 % d0) + n0, d0) + b * (n1 / d0);
	} else {
		/* 다음에서 제공되는 알고리즘과 증명을 바탕으로 한다:
		 * http://www.hackersdelight.org/revisions.pdf. */
		if (n < d)
			return 0;
		else {
			uint32_t d1 = d >> 32;
			int s = nlz (d1);
			uint64_t q = divl (n >> 1, (d << s) >> 32) >> (31 - s);
			return n - (q - 1) * d < d ? q - 1 : q;
		}
	}
}

/* 부호 없는 64비트 N을 부호 없는 64비트 D로 나누고 나머지를 반환한다. */
static uint32_t
umod64 (uint64_t n, uint64_t d) {
	return n - d * udiv64 (n, d);
}

/* 부호 있는 64비트 N을 부호 있는 64비트 D로 나누고 몫을 반환한다. */
static int64_t
sdiv64 (int64_t n, int64_t d) {
	uint64_t n_abs = n >= 0 ? (uint64_t) n : -(uint64_t) n;
	uint64_t d_abs = d >= 0 ? (uint64_t) d : -(uint64_t) d;
	uint64_t q_abs = udiv64 (n_abs, d_abs);
	return (n < 0) == (d < 0) ? (int64_t) q_abs : -(int64_t) q_abs;
}

/* 부호 있는 64비트 N을 부호 있는 64비트 D로 나누고 나머지를 반환한다. */
static int32_t
smod64 (int64_t n, int64_t d) {
	return n - d * sdiv64 (n, d);
}

/* 이들은 GCC가 호출하는 루틴들이다. */

long long __divdi3 (long long n, long long d);
long long __moddi3 (long long n, long long d);
unsigned long long __udivdi3 (unsigned long long n, unsigned long long d);
unsigned long long __umoddi3 (unsigned long long n, unsigned long long d);

/* 부호 있는 64비트 나눗셈. */
long long
__divdi3 (long long n, long long d) {
	return sdiv64 (n, d);
}

/* 부호 있는 64비트 나머지. */
long long
__moddi3 (long long n, long long d) {
	return smod64 (n, d);
}

/* 부호 없는 64비트 나눗셈. */
unsigned long long
__udivdi3 (unsigned long long n, unsigned long long d) {
	return udiv64 (n, d);
}

/* 부호 없는 64비트 나머지. */
unsigned long long
__umoddi3 (unsigned long long n, unsigned long long d) {
	return umod64 (n, d);
}
