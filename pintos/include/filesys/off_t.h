#ifndef FILESYS_OFF_T_H
#define FILESYS_OFF_T_H

#include <stdint.h>

/* 파일 내부 offset.
 * 여러 header가 이 정의만 원하고 다른 것은 원하지 않기 때문에
 * 별도 header로 분리되어 있다. */
typedef int32_t off_t;

/* printf()용 format specifier, 예:
 * printf ("offset=%"PROTd"\n", offset); */
#define PROTd PRId32

#endif /* filesys/off_t.h */
