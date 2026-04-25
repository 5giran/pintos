#include "userprog/tss.h"
#include <debug.h>
#include <stddef.h>
#include "userprog/gdt.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "intrinsic.h"

/* The Task-State Segment (TSS).
 *
 * TSS의 인스턴스는 x86-64 전용 구조로, 프로세서에 내장된 멀티태스킹 지원의 한 형태인 "tasks"를 정의하는 데 사용된다.
 * 그러나 이식성, 속도, 유연성 등의 이유로 대부분의 x86-64 OS는 TSS를 거의 완전히 무시한다. 우리도 예외가 아니다.
 *
 * 아쉽게도 TSS를 사용해야만 할 수 있는 일이 하나 있다: user mode에서 발생하는 interrupt를 위한 stack
 * switching이다. user mode(ring 3)에서 interrupt가 발생하면, 프로세서는 현재 TSS의 rsp0 멤버를
 * 참고하여 해당 interrupt 처리를 위해 사용할 stack을 결정한다. 따라서 우리는 TSS를 만들고 최소한 이 필드들을 초기화해야
 * 하며, 이 파일이 바로 그 작업을 수행한다.
 *
 * interrupt 또는 trap gate(우리가 처리하는 모든 interrupt에 적용됨)로 interrupt가 처리될 때,
 * x86-64 프로세서는 다음과 같이 동작한다:
 *
 * - interrupt에 의해 중단된 코드가 interrupt handler와 같은 ring에 있으면 stack switch는 발생하지
 * 않는다. 이는 우리가 kernel에서 실행 중일 때 발생하는 interrupt의 경우다. 이 경우 TSS의 내용은 무관하다.
 *
 * - interrupt에 의해 중단된 코드가 handler와 다른 ring에 있으면, 프로세서는 TSS에 지정된 새 ring의
 * stack으로 전환한다. 이는 우리가 user space에 있을 때 발생하는 interrupt의 경우다. 손상을 피하려면 이미 사용
 * 중이 아닌 stack으로 전환하는 것이 중요하다. 우리는 user space에서 실행 중이므로, 현재 프로세스의 kernel
 * stack은 사용 중이 아님을 알고 있으니 항상 그것을 사용할 수 있다. 따라서 scheduler가 thread를 전환할 때, TSS의
 * stack pointer도 새 thread의 kernel stack을 가리키도록 바꾼다. (호출은 thread.c의 schedule에
 * 있다.) */

/* 커널 TSS. */
struct task_state *tss;

/* 커널 TSS를 초기화한다. */
void
tss_init (void) {
	/* 우리의 TSS는 call gate나 task gate에서 사용되지 않으므로, 참조되는 필드는 몇 개뿐이며, 우리는 그 필드들만
	 * 초기화한다. */
	tss = palloc_get_page (PAL_ASSERT | PAL_ZERO);
	tss_update (thread_current ());
}

/* 커널 TSS를 반환한다. */
struct task_state *
tss_get (void) {
	ASSERT (tss != NULL);
	return tss;
}

/* TSS의 ring 0 stack pointer를 thread stack의 끝을 가리키도록 설정한다. */
void
tss_update (struct thread *next) {
	ASSERT (tss != NULL);
	tss->rsp0 = (uint64_t) next + PGSIZE;
}
