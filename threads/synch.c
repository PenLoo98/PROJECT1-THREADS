/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_push_back (&sema->waiters, &thread_current ()->elem);
		list_sort(&sema->waiters, priority_cmp, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;
	//선점 구현위해 구조체 정의
	struct thread *t;
	ASSERT (sema != NULL);
	/*project 1.2:priority donation*/
	//꺼낼때도 정렬한번하고 깨워야함
	list_sort (&sema->waiters, priority_cmp, NULL);	
	/*project 1.2:priority donation*/	
	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)){
		//언블락하는 스레드t에 저장
		thread_unblock (t=list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	}
	sema->value++;
	intr_set_level (old_level);
	//만약 언블락된스레드가 지금실행스레드보다 더 세다면
	if(t->priority > thread_current()->priority){
		//선점하기
		thread_yield();
	}
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	/*project 1.2:priority donation*/
	//만약 지금 락에 홀더가 있으면
	//단순함을 위해 도네리스트에 대소비교 안하고 싹 다넣기로함
	if(lock->holder !=NULL){
		// 대기하고있는 락 설정하고
		thread_current()->wait_on_lock = lock;
		//대기열에 삽입
		list_insert_ordered(&lock->holder->donations,&thread_current()->donation_elem,donation_priority_cmp,NULL);
		//연쇄 기부
		chaining_donate_priority();
	}
	/*project 1.2:priority donation*/

	sema_down (&lock->semaphore);
	/*project 1.2:priority donation*/
	//이게 실행된단건 내가 살아나서 락을 쥐었다는거니까
	thread_current()->wait_on_lock=NULL;
	/*project 1.2:priority donation*/	
	lock->holder = thread_current ();
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	/*project 1.2:priority donation*/
	//락 대기열에 있던 애들 다 도네리스트에서 제거하기
	remove_with_lock(lock);
	//도네리스트 바뀌었으니까 우선순위 재조정
	refresh_priority();
	/*project 1.2:priority donation*/
	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	// list_push_back (&cond->waiters, &waiter.elem);
	// 맞는위치에 삽입
	list_insert_this_thread_ordered(&cond->waiters,&((&waiter)->elem),cnd_priority_cmp,NULL);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters))
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

//컨디션용 쓰레드 정렬로직
bool cnd_priority_cmp(const struct list_elem *a,const struct list_elem *b,void *aux){
	// a는 현스레드 새마_elem, b는 elem
	struct list_elem* x;
	x=list_begin(&list_entry(b,struct semaphore_elem,elem)->semaphore.waiters);
	int64_t x_priority=list_entry(x,struct thread,elem)->priority;
	return thread_current()->priority > x_priority;
	// return list_entry(a,struct semaphore_elem,elem)->semaphore.waiters > list_begin(&list_entry(b,struct semaphore_elem,elem)->semaphore.waiters)
}

//현재의 쓰레드를 컨디션 대기열에 넣는 함수
// list_insert_thread_ordered(대기열,현재스레드가 들어갈 세마_elem의 elem,대소비교함수,NULL);
void
list_insert_this_thread_ordered (struct list *list, struct list_elem *elem,
		list_less_func *less, void *aux) {
	struct list_elem *e;

	ASSERT (list != NULL);
	ASSERT (elem != NULL);
	ASSERT (less != NULL);

	for (e = list_begin (list); e != list_end (list); e = list_next (e))
		if (less (elem, e, aux))
			break;
	return list_insert (e, elem);
}

//도네이션 내림차순판단용 함수
bool donation_priority_cmp(const struct list_elem *a,const struct list_elem *b,void *aux){
	// a는 현스레드 새마_elem, b는 elem
	return list_entry(a,struct thread,donation_elem)->priority > list_entry(b,struct thread,donation_elem)->priority;
}

//연쇄적으로 연결된 락들 우선순위 변경
void chaining_donate_priority(void){
	struct thread *cur = thread_current();
	//체인라이트닝 끝날때까지
	while(1){
		//대기중인 락이 없거나 (이런일이 있을까 싶음) 락홀더가 나보다 세면
		if((cur->wait_on_lock == NULL )||(cur->wait_on_lock ->holder->priority >= cur->priority)){
			break;
		}
		//체인라이트닝 타고 들어간 내가 락홀더보다 세면
		else if(cur->wait_on_lock ->holder->priority < cur->priority){
			//락홀더 우선순위 업뎃
			cur->wait_on_lock ->holder->priority = cur->priority;
			//정렬도 해줘야될거같은데...
			list_sort (&cur->donations, donation_priority_cmp, NULL);
			cur = cur->wait_on_lock->holder;
		}
	}
}

//락 대기열에 있던 애들 다 도네리스트에서 제거하기. (왜 하나만 하지...?)
void remove_with_lock(struct lock* lock){
	struct list_elem *e;
 	struct thread *cur = thread_current ();

  	for (e = list_begin (&cur->donations); e != list_end (&cur->donations); e = list_next (e)){
		struct thread *t = list_entry (e, struct thread, donation_elem);
		if (t->wait_on_lock == lock)
		list_remove (&t->donation_elem);
	}
}

//현재 스레드 우선순위 리프레시
void refresh_priority(void){
	//정렬 함 해주고(미리해줄거라 불필요할거같은데...)
	list_sort (&thread_current()->donations, donation_priority_cmp, NULL);
	//찐 우선순위를 내 우선순위로
	thread_current()->priority=thread_current()->init_priority;
	// 도네리스트가 비지 않았다면
	if(!list_empty(&thread_current()->donations)){
		//도네리스트 짱이 내 찐 우선순위보다 세면
		if(list_entry(list_begin(&thread_current()->donations),struct thread,donation_elem)->priority > thread_current()->init_priority){
			//도네리스트 짱을 내 우선순위로
			thread_current()->priority=list_entry(list_begin(&thread_current()->donations),struct thread,donation_elem)->priority ;
		}
	}
}