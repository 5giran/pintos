#ifndef __LIB_STDDEF_H
#define __LIB_STDDEF_H

#define NULL ((void *) 0)
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *) 0)->MEMBER)

/* GCC가 ptrdiff_t와 size_t에 필요한 타입을 미리 정의해 두므로,
 * 우리가 추측할 필요가 없다. */
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __SIZE_TYPE__ size_t;

#endif /* lib/stddef.h */
