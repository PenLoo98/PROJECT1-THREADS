#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
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
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */

	int64_t wakeup_tick; // thread가 깨어날 시간

	/* Priority Donation 관련 멤버 */
	int init_priority; // thread의 초기 priority
	struct lock *wait_on_lock; // thread가 기다리는 lock
	struct list donations; // 이 스레드가 가진 lock을 기다리는 스레드들의 리스트
	struct list_elem d_elem; // 특정 lock을 가진 스레드를 기다릴 때 사용되는 elem

	/* MLFQS 관련 멤버 */
	int nice; // 높을 수록 우선순위 낮아짐
	int recent_cpu; // 최근에 얼마나 많은 CPU time을 사용했는지를 나타내는 변수
	struct list_elem all_elem; // 모든 thread들을 관리하는 elem

	/* 프로세스 계층 구조 관련 멤버 */
	struct thread *parent; // 부모 thread
	struct list_elem child_elem; // 누군가의 자식 프로세스로 리스트에 삽입될 때 사용되는 elem
	struct list child_list; // 자식 thread들을 관리하는 리스트

	bool is_memory_loaded; // 메모리에 로드되었는지
	bool is_exit; // 종료되었는지
	bool is_exit_called; // exit()가 호출되었는지
	struct semaphore exit_sema; // 종료될 때 부모 스레드가 자식 스레드의 종료 시간을 주기 위한 세마포어
	struct semaphore wait_sema; // wait() 시에 부모 스레드가 자식 스레드의 종료를 기다리기 위한 세마포어
	struct semaphore load_sema; // load() 시에 부모 스레드가 자식 스레드의 메모리 로드를 기다리기 위한 세마포어
	tid_t exit_status; // 종료 상태: status와 달리 exit() 시에만 사용됨

	/* userprog 관련 함수 */
	struct file **fd_table; // 파일 디스크립터 테이블
	int next_fd; // 다음에 할당할 파일 디스크립터

	struct intr_frame parent_if; // 부모 프로세스의 intr_frame
	struct file *running_file; // 실행 중인 파일

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
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

void mlfqs_priority (struct thread *t);
void mlfqs_recent_cpu (struct thread *t);
void mlfqs_load_avg (void);
void mlfqs_increment (void);
void mlfqs_recalc (void);
int64_t earliest_wakeup_time;

void do_iret (struct intr_frame *tf);

bool donations_higher_priority(const struct list_elem *a_, 
			const struct list_elem *b_, void *aux UNUSED);
void donate_priority(void);
void remove_with_lock(struct lock *lock);
void refresh_priority(void);

#endif /* threads/thread.h */
