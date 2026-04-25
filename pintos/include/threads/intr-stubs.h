#ifndef THREADS_INTR_STUBS_H
#define THREADS_INTR_STUBS_H

/* interrupt stub들.
 *
 * 이것들은 intr-stubs.S 안의 작은 코드 조각들로,
 * 가능한 256개의 x86 interrupt 각각에 하나씩 존재한다.
 * 각 조각은 약간의 stack 조작을 한 뒤 intr_entry()로 점프한다.
 * 자세한 내용은 intr-stubs.S를 참고하라.
 *
 * 이 배열은 각 interrupt stub 진입점을 가리키며,
 * intr_init()이 이를 쉽게 찾을 수 있게 해 준다. */
typedef void intr_stub_func (void);
extern intr_stub_func *intr_stubs[256];

#endif /* threads/intr-stubs.h */
