/* 이 파일은 MIT의 6.828 강의에서 사용된 소스 코드에서 파생되었다.
   원본 저작권 고지는 아래에 전문을 다시 실었다. */

/*
 * Copyright (C) 1997 Massachusetts Institute of Technology
 *
 * 이 소프트웨어는 저작권 보유자가 아래 라이선스에 따라 제공합니다.
 * 이 소프트웨어를 취득, 사용 및/또는 복제함으로써, 귀하는 아래
 * 조건을 읽고 이해했으며 이를 준수하는 데 동의합니다:
 *
 * 이 소프트웨어와 그 문서를 어떤 목적이든 수수료나 로열티 없이
 * 사용, 복제, 수정, 배포, 판매할 수 있는 권한을 부여합니다.
 * 단, 귀하가 만든 수정본을 포함하여 소프트웨어와 문서의 전체 복사본 또는
 * 일부마다 이 NOTICE 전문이 포함되어야 합니다.
 *
 * 이 소프트웨어는 "AS IS" 상태로 제공되며, 저작권 보유자는 명시적이든
 * 묵시적이든 어떠한 진술이나 보증도 하지 않습니다. 예시일 뿐이며 이에
 * 한정되지 않게, 저작권 보유자는 상품성 또는 특정 목적 적합성에 대한 보증이나
 * 이 소프트웨어 또는 문서의 사용이 제3자의 특허, 저작권, 상표 또는 기타 권리를
 * 침해하지 않는다는 보증을 하지 않습니다. 저작권 보유자는 이 소프트웨어나
 * 문서의 사용에 대해 어떠한 책임도 지지 않습니다.
 *
 * 저작권 보유자의 이름과 상표는 사전에 구체적인 서면 허가를 받지 않는 한
 * 이 소프트웨어와 관련된 광고나 홍보에 사용해서는 안 됩니다. 이 소프트웨어와
 * 관련 문서의 저작권은 항상 저작권 보유자에게 귀속됩니다. 모든 저작권
 * 보유자 목록은 이 소프트웨어와 함께 제공되었어야 하는 AUTHORS 파일을 보십시오.
 *
 * 이 파일은 이전에 저작권이 있던 소프트웨어에서 파생되었을 수 있습니다.
 * 이 저작권은 AUTHORS 파일에 나열된 저작권 보유자가 가한 변경 사항에만
 * 적용됩니다. 이 파일의 나머지 부분은 아래에 적힌 저작권 고지(있다면)의
 * 적용을 받습니다.
 */

#ifndef THREADS_IO_H
#define THREADS_IO_H

#include <stddef.h>
#include <stdint.h>

/* PORT에서 byte 하나를 읽어 반환한다. */
static inline uint8_t
inb (uint16_t port) {
	/* [IA32-v2a] "IN"을 참고하라. */
	uint8_t data;
	asm volatile ("inb %w1,%0" : "=a" (data) : "d" (port));
	return data;
}

/* PORT에서 CNT byte를 차례로 읽어 ADDR부터 시작하는 buffer에 저장한다. */
static inline void
insb (uint16_t port, void *addr, size_t cnt) {
	/* [IA32-v2a] "INS"를 참고하라. */
	asm volatile ("cld; repne; insb"
			: "=D" (addr), "=c" (cnt)
			: "d" (port), "0" (addr), "1" (cnt)
			: "memory", "cc");
}

/* PORT에서 16비트를 읽어 반환한다. */
static inline uint16_t
inw (uint16_t port) {
	uint16_t data;
	/* [IA32-v2a] "IN"을 참고하라. */
	asm volatile ("inw %w1,%0" : "=a" (data) : "d" (port));
	return data;
}

/* PORT에서 CNT개의 16-bit(halfword) 단위를 차례로 읽어
   ADDR부터 시작하는 buffer에 저장한다. */
static inline void
insw (uint16_t port, void *addr, size_t cnt) {
	/* [IA32-v2a] "INS"를 참고하라. */
	asm volatile ("cld; repne; insw"
			: "=D" (addr), "=c" (cnt)
			: "d" (port), "0" (addr), "1" (cnt)
			: "memory", "cc");
}

/* PORT에서 32비트를 읽어 반환한다. */
static inline uint32_t
inl (uint16_t port) {
	/* [IA32-v2a] "IN"을 참고하라. */
	uint32_t data;
	asm volatile ("inl %w1,%0" : "=a" (data) : "d" (port));
	return data;
}

/* PORT에서 CNT개의 32-bit(word) 단위를 차례로 읽어
   ADDR부터 시작하는 buffer에 저장한다. */
static inline void
insl (uint16_t port, void *addr, size_t cnt) {
	/* [IA32-v2a] "INS"를 참고하라. */
	asm volatile ("cld; repne; insl"
			: "=D" (addr), "=c" (cnt)
			: "d" (port), "0" (addr), "1" (cnt)
			: "memory", "cc");
}

/* DATA byte를 PORT에 쓴다. */
static inline void
outb (uint16_t port, uint8_t data) {
	/* [IA32-v2b] "OUT"을 참고하라. */
	asm volatile ("outb %0,%w1" : : "a" (data), "d" (port));
}

/* ADDR부터 시작하는 CNT-byte buffer의 각 byte를 PORT에 쓴다. */
static inline void
outsb (uint16_t port, const void *addr, size_t cnt) {
	/* [IA32-v2b] "OUTS"를 참고하라. */
	asm volatile ("cld; repne; outsb"
			: "=S" (addr), "=c" (cnt)
			: "d" (port), "0" (addr), "1" (cnt)
			: "cc");
}

/* 16-bit DATA를 PORT에 쓴다. */
static inline void
outw (uint16_t port, uint16_t data) {
	/* [IA32-v2b] "OUT"을 참고하라. */
	asm volatile ("outw %0,%w1" : : "a" (data), "d" (port));
}

/* ADDR부터 시작하는 CNT-halfword buffer의 각 16-bit 단위를 PORT에 쓴다. */
static inline void
outsw (uint16_t port, const void *addr, size_t cnt) {
	/* [IA32-v2b] "OUTS"를 참고하라. */
	asm volatile ("cld; repne; outsw"
			: "=S" (addr), "=c" (cnt)
			: "d" (port), "0" (addr), "1" (cnt)
			: "cc");
}

/* 32-bit DATA를 PORT에 쓴다. */
static inline void
outl (uint16_t port, uint32_t data) {
	/* [IA32-v2b] "OUT"을 참고하라. */
	asm volatile ("outl %0,%w1" : : "a" (data), "d" (port));
}

/* ADDR부터 시작하는 CNT-word buffer의 각 32-bit 단위를 PORT에 쓴다. */
static inline void
outsl (uint16_t port, const void *addr, size_t cnt) {
	/* [IA32-v2b] "OUTS"를 참고하라. */
	asm volatile ("cld; repne; outsl"
			: "=S" (addr), "=c" (cnt)
			: "d" (port), "0" (addr), "1" (cnt)
			: "cc");
}

#endif /* threads/io.h */
