#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* thread 생명 주기의 상태들. */
enum thread_status {
	THREAD_RUNNING,     /* 실행 중인 thread. */
	THREAD_READY,       /* 실행 중은 아니지만 실행 준비가 된 상태. */
	THREAD_BLOCKED,     /* 어떤 event가 발생하기를 기다리는 상태. */
	THREAD_DYING        /* 곧 파기될 상태. */
};

/* thread 식별자 타입.
   원하는 타입으로 재정의해도 된다. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* tid_t의 오류 값. */

/* thread priority(스레드 우선순위). */
#define PRI_MIN 0                       /* 가장 낮은 priority. */
#define PRI_DEFAULT 31                  /* 기본 priority. */
#define PRI_MAX 63                      /* 가장 높은 priority. */

/* kernel thread 또는 user process.
 *
 * 각 thread 구조체는 자기 전용 4 kB page에 저장된다.
 * thread 구조체 자체는 페이지의 맨 아래(offset 0)에 놓인다.
 * 페이지의 나머지 부분은 thread의 kernel stack 용도로 예약되며,
 * kernel stack은 페이지의 맨 위(offset 4 kB)에서 아래로 자란다.
 * 그림으로 보면 다음과 같다:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * 여기서 얻을 수 있는 결론은 두 가지다:
 *
 *    1. 첫째, `struct thread'가 너무 커지면 안 된다.
 *       너무 커지면 kernel stack을 둘 공간이 부족해진다.
 *       기본 `struct thread'는 겨우 몇 바이트 크기다.
 *       아마 1 kB보다 훨씬 작게 유지되어야 한다.
 *
 *    2. 둘째, kernel stack도 너무 커지면 안 된다.
 *       stack이 overflow되면 thread 상태를 손상시킨다.
 *       따라서 kernel 함수는 큰 구조체나 배열을 non-static 지역 변수로
 *       할당해서는 안 된다. 대신 malloc()이나 palloc_get_page() 같은
 *       동적 할당을 사용하라.
 *
 * 이 두 문제 중 어느 것이든 첫 증상은 대개 thread_current()에서의
 * assertion failure일 것이다. 이 함수는 현재 실행 중인 thread의
 * `struct thread'에 있는 `magic' 멤버가 THREAD_MAGIC으로 설정되어 있는지
 * 검사한다. stack overflow가 발생하면 보통 이 값이 바뀌어 assertion이 터진다. */
/* `elem' 멤버는 두 가지 용도를 가진다.
 * run queue(thread.c)의 원소가 될 수도 있고,
 * semaphore 대기 리스트(synch.c)의 원소가 될 수도 있다.
 * 두 용도가 상호 배타적이기 때문에 이렇게 사용할 수 있다.
 * ready 상태의 thread만 run queue에 있고,
 * blocked 상태의 thread만 semaphore 대기 리스트에 있기 때문이다. */
struct thread {
	/* thread.c가 소유한다. */
	tid_t tid;                          /* thread 식별자. */
	enum thread_status status;          /* thread 상태. */
	char name[16];                      /* 이름(디버깅용). */
	int priority;                       /* priority(우선순위). */

	/* thread.c와 synch.c가 공유한다. */
	struct list_elem elem;              /* list 원소. */

#ifdef USERPROG
	/* userprog/process.c가 소유한다. */
	uint64_t *pml4;                     /* PML4 테이블 */
#endif
#ifdef VM
	/* thread가 소유한 전체 virtual memory에 대한 테이블. */
	struct supplemental_page_table spt;
#endif

	/* thread.c가 소유한다. */
	struct intr_frame tf;               /* 전환에 필요한 정보. */
	unsigned magic;                     /* stack overflow를 감지한다. */
};

/* false(기본값)면 round-robin scheduler를 사용한다.
   true면 multi-level feedback queue scheduler를 사용한다.
   kernel command-line option "-o mlfqs"로 제어한다. */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */
