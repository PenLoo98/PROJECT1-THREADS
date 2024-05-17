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
struct semaphore_elem* condition_pop_max(struct condition *cond);

static bool biggerThan(const struct list_elem *a,const struct list_elem *b,void *aux);

static bool biggerThan(const struct list_elem *a,const struct list_elem *b,void *aux){
	int aPriority = list_entry(a,struct thread,elem) -> priority;
	int bPriority = list_entry(b,struct thread,elem) -> priority;
	return aPriority>bPriority;
}

static struct thread* element_to_thread(struct list_elem *element){
	return list_entry(element,struct thread,elem);
}

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   세마포어는 두개의 원자적 연산으로 다루어진다.
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
	//0이면 while문에서 벗어나지 못함.
	while (sema->value == 0) {
		list_insert_ordered (&sema->waiters, &thread_current()->elem, biggerThan, NULL);
		//현재 스레드를 블록 상태로 바꾸고 컨텍스트 스위칭(다음 스레드 실행)
		thread_block ();
	}
	//깨어나자 마자 세마 값을 낮추면서 세마 점유
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

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	list_sort(&sema->waiters,biggerThan,NULL);
	if (!list_empty (&sema->waiters))
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	sema->value++;

	comapare_and_preemtion();

	intr_set_level (old_level);
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
/* 만약 다른 스레드가 세마포어를 소유하고 있다면, lock에서 탈출하지 못하고 있음.
   처음으로 세마포어를 가져오는 것이라면 lock을 소유하면서 자신을 소유자로 지정함.
*/
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	enum intr_level older_level =  intr_disable();

	//lock holder가 있다면 자기는 잠들고 우선순위를 전파해야됨.
	//우선 순위를 전파했다면 lock holder는 자신을 lock 시킨 thread를 찾아서 우선 순위 전파
	if(lock->holder != NULL){
		struct thread* cur = thread_current();
		//lock의 waiters가 비어있다면 lock holder의 donations에 자신을 추가함,
		//비어 있지 않은데 waiters에 있는 것들 보다 자신이 크다면 waiters의 가장 큰 값을 donations에서 지우고
		//자신을 donations에 추가
		struct list* waiters_of_sem = &(lock->semaphore.waiters);
		if(list_empty(&(lock->semaphore.waiters))){
			list_push_back(&(lock->holder->donations),&cur->d_elem);
		}
		//waiters의 가장 높은 우선순위보다 현재 스레드의 우선순위가 높으면  
		else if(cur->priority >= element_to_thread(list_begin(waiters_of_sem))->priority){
			//가장 우선 순위 높았던 waiters의 스레드를 donations에서 제거하고, 현재 스레드를 추가
			list_remove(&element_to_thread(list_begin(waiters_of_sem))->d_elem);
			list_push_back(&(lock->holder->donations),&cur->d_elem);
		}
		//나의 wait_on_lock을 지정해주고 여기서 wait on lock에 thead가 아니라 holder를 지정해야함
		cur->wait_on_lock = lock;
		//스레드가 lock holder에 비해 우선 순위가 높다면
		//나의 우선 순위가 lock holder의 현재 우선 순위보다 크다면 lock_holder에게 넘기고
		if(cur->priority > lock->holder->priority){
			lock->holder->priority = cur->priority;
			//nested_donation 진행
			for(struct lock* nextlock = lock ; nextlock = nextlock->holder->wait_on_lock ; nextlock!=NULL){
				//만약 현재의 우선순위가 lock holder의 priority보다 작거나 같으면 전파할 필요 없음. 
				if(nextlock->holder->priority >= cur->priority)
					break;
				nextlock->holder->priority = cur->priority;
			}
		}
	}
	sema_down (&lock->semaphore);
	thread_current()->wait_on_lock == NULL;
	intr_set_level(older_level);
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

//donations에서 max를 찾기 위해 사용하는 함수
static bool donations_smaller_than(const struct list_elem *a,const struct list_elem *b,void *aux){
	struct thread* aThread = list_entry(a,struct thread,elem);
	struct thread* bThread = list_entry(b,struct thread,elem);

	int aPriority = aThread->priority;
	int bPriority = bThread->priority;

	return aPriority<bPriority;
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
	
	// enum intr_level old_level = intr_disable();
	lock->holder = NULL;
	//lock을 버리면서 도네이션 중 이 lock을 가지고 있던 스레드를 제거한다.
	//donations에서 제거하면 도네이션의 최대값과 initial priority를 비교하여 
	//가장 큰 값을 자신의 priority로 바꾼다.
	struct list* sem_list = &(lock->semaphore.waiters);
	//리스트가 비어있지 않다면 donations의 스레드가 waiters의 스레드 안에 들어있음.
	if(!list_empty(sem_list)){
		int initial_priority = thread_current()->initial_priority;

		//제거할 스레드는 도네이션에 속한 리스트중 지금 락에 걸려 있는 스레드
		//도네이션을 순회하면서 지금 lock에 걸린 
		struct thread* thread_to_remove = element_to_thread(list_begin(&(lock->semaphore.waiters)));
		list_remove(&(thread_to_remove->d_elem));
		//donations의 리스트가 비어있지 않다면 최대 값을 뽑아서 initial priority와 비교해 큰 값 넣기
		if(!list_empty(&thread_current()->donations)){
			int dona_max = element_to_thread(list_max(&thread_current()->donations,donations_smaller_than,NULL))->priority;
			if(dona_max>initial_priority){
				thread_current()->priority = dona_max;
			}
			else{
				thread_current()->priority = initial_priority;
			}
		}
		else{
			thread_current()->priority = initial_priority;
		}
	}
	sema_up (&lock->semaphore);
	// intr_set_level(old_level);
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
   /*
   컨디션 변수는 항상 lock이 걸려 있는 상황에서 사용한다.
   초반에 lock을 반환하여 signal이 발생하는 상황을 기다리고, 
   */
void
cond_wait (struct condition *cond, struct lock *lock) {
	//세마포어와 리스트 요소를 들고 있는 구조체
	//즉 세마포어를 어떤 리스트에 넣고 싶은 상황이다.
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	//0인 세마포어는 항상 재운다.
	sema_init (&waiter.semaphore, 0);
	//waiter를 컨디션 변수 큐에 삽입한다.
	list_push_back (&cond->waiters, &waiter.elem);
	//lock을 realease한다. 즉 signal이 접근할 수 있다.
	lock_release (lock);

	// intr_disable();
	//자신을 재운다.
	sema_down (&waiter.semaphore);
	//자신을 깨우면서 lock을 재획득 해야함.
	// intr_enable();

	lock_acquire (lock);
}

//a가b보다 클때 true return
static bool sem_smaller_than(const struct list_elem *a,const struct list_elem *b,void *aux){
	struct semaphore* aSema = &list_entry(a,struct semaphore_elem,elem)->semaphore;
	struct semaphore* bSema = &list_entry(b,struct semaphore_elem,elem)->semaphore;

	int aPriority = element_to_thread(list_begin(&aSema->waiters))->priority;
	int bPriority = element_to_thread(list_begin(&bSema->waiters))->priority;

	return aPriority<bPriority;
}
/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */


/*컨디션 변수에서 가장 우선순위가 높은 세마포어를 리턴하는 함수
*/
struct semaphore_elem*
condition_pop_max(struct condition *cond){
	if (!list_empty (&cond->waiters)){
		struct semaphore_elem* max_sema_elem = list_entry (list_max(&cond->waiters,sem_smaller_than,NULL),
					struct semaphore_elem, elem);
		list_remove(max_sema_elem);
		return max_sema_elem;
	}
}

void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	intr_disable();
	if (!list_empty (&cond->waiters))
	//컨디션 변수에서 하나의 세마포어를 꺼내고, 그 세마포어에 막혀 있던 스레드를 깨운다.
		sema_up (&condition_pop_max(cond)->semaphore);
	intr_enable();
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
