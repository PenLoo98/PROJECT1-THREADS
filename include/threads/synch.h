#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* A counting semaphore. */
struct semaphore {
	unsigned value;             /* Current value. */
	struct list waiters;        /* List of waiting threads. */
};

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

/* Lock. */
struct lock {
	struct thread *holder;      /* Thread holding lock (for debugging). */
	struct semaphore semaphore; /* Binary semaphore controlling access. */
};

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* Condition variable. */
struct condition {
	struct list waiters;        /* List of waiting threads. */
};

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

/* Optimization barrier.
 *
 * The compiler will not reorder operations across an
 * optimization barrier.  See "Optimization Barriers" in the
 * reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")

//컨디션용 쓰레드 정렬로직
// bool cnd_priority_cmp(const struct list_elem *a,const struct list_elem *b,void *aux);

void
list_insert_this_thread_ordered (struct list *list, struct list_elem *elem,
		list_less_func *less, void *aux);

bool cnd_priority_cmp(const struct list_elem *a,const struct list_elem *b,void *aux);
//도네이션 대소판별용 함수
bool donation_priority_cmp(const struct list_elem *a,const struct list_elem *b,void *aux);
//연쇄적으로 연결된 락들 우선순위 변경
void chaining_donate_priority(void);
//락 대기열에 있던 애들 다 도네리스트에서 제거하기. 일단은 하나만
void remove_with_lock(struct lock* lock);
//현재 스레드 우선순위 리프레시
void refresh_priority(void);

#endif /* threads/synch.h */
