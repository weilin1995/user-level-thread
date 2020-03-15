/* Simplethreads Instructional Thread Package
 * 
 * sthread_user.c - Implements the sthread API using user-level threads.
 *
 *    You need to implement the routines in this file.
 *
 * Change Log:
 * 2002-04-15        rick
 *   - Initial version.
 */

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sthread.h>
#include <sthread_queue.h>
#include <sthread_user.h>
#include <sthread_ctx.h>
#include <sthread_user.h>

#include <sthread_preempt.h>
#include <stdbool.h>



/* Debug printing */
//http://stackoverflow.com/questions/1644868/c-define-macro-for-debug-printing
#define DEBUG 0

#define debug_print(fmt, ...) \
	do { if (DEBUG) fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, \
			__LINE__, __func__, __VA_ARGS__); } while (0)


struct _sthread {
	sthread_ctx_t *saved_ctx;
	/* Add your fields to the thread data structure here */
	int tid; // thread id
	sthread_start_func_t start_routine;
	void *arg;
	bool joinable;
	bool finished;
	void* ret;
};

int global_thread_id = 0; //used to keep track of thread ids

int alive_thread = 0; //counter for number of alive threads
lock_t alive_thread_lock = 0; // lock which protects alive_thread counter

void dummyFunc();
void clean_dead_threads();
void switch_thread();
void preempt_func();

sthread_queue_t ready_list; // stores threads that can be contexted switched to
sthread_queue_t dead_queue; // stores threads that need to be freed
//sthread_queue_t waiting_list;
//lock_t ready_list_lock;
//lock_t dead_queue_lock;

sthread_t main_thread;
sthread_t cur_running_thread;
/*********************************************************************/
/* Part 1: Creating and Scheduling Threads                           */
/*********************************************************************/

void sthread_user_init(void) {
	ready_list = sthread_new_queue();
	dead_queue = sthread_new_queue();
	main_thread = malloc(sizeof(struct _sthread));
	main_thread->saved_ctx = sthread_new_blank_ctx();
	main_thread->joinable = false;
	main_thread->finished = false; 
	main_thread->tid = 0;
	cur_running_thread = main_thread;
	sthread_preemption_init(&preempt_func, 50);
}

sthread_t sthread_user_create(sthread_start_func_t start_routine, void *arg,
		int joinable) {

	// Creates a new thread and adds it to the ready queue. Use a dummy function for the ctx.
	sthread_t new_thread = malloc(sizeof(struct _sthread));
	new_thread->start_routine = start_routine;
	new_thread->arg = arg;
	new_thread->joinable = (joinable == 1);
	new_thread->saved_ctx = sthread_new_ctx(&dummyFunc);
	new_thread->finished = false;
	new_thread->tid = ++global_thread_id; 
	debug_print("created a thread TID %i\n", new_thread->tid);	
	
	// disable interrupts when adding the thread to the ready_list
	int old = splx(HIGH);
	sthread_enqueue(ready_list, new_thread);
	alive_thread++;
	splx(old);

	return new_thread;
}

void sthread_user_exit(void *ret) {
	clean_dead_threads();

	debug_print("TID %i is exiting\n", cur_running_thread->tid);	
	int old_val = splx(HIGH); // disable interupt here cause we don't want the finished thread to be in ready queue
	
	// make the thread as finished
	cur_running_thread->ret = ret;
	cur_running_thread->finished = true;

	// If the thread is not joinable, add it to the dead queue to be free'd later
	if (!cur_running_thread->joinable) {
		sthread_enqueue(dead_queue, cur_running_thread);
	}

	switch_thread(cur_running_thread);
	splx(old_val);
}

void* sthread_user_join(sthread_t t) {
	clean_dead_threads();

	if (t == NULL) {
		debug_print("%s\n","joining with a null thread (possibly terminated/freed) returning...");
		return NULL;
	}

	debug_print("trying to join thread %i joined with %i \n", cur_running_thread->tid, t->tid);
	
	// if the thread is not finished yet, yield to another thread
	while(!t->finished) {
		sthread_user_yield();
	}

	void* ret = t->ret;
	debug_print("joined thread %i joined with %i \n", cur_running_thread->tid, t->tid);
	debug_print("freeing dead thread from join, tid = %i \n", t->tid);
	debug_print("ready size: %i ; dead size: %i\n", sthread_queue_size(ready_list), sthread_queue_size(dead_queue));

	// Free the thread since the thread has joined
	sthread_free_ctx(t->saved_ctx);
	free(t);

	// decrement the alive count
	while(atomic_test_and_set(&alive_thread_lock)) {};
	alive_thread--;
	atomic_clear(&alive_thread_lock);

	// if this is the last alive thread clean the free list of the queue
	if (alive_thread == 0) {
		sthread_queue_clear_free_list();
		debug_print("%s\n", "cleaned the queue");

	}
	return ret;
}

void sthread_user_yield(void) {

	// If current thread is not finished yet, add it to the ready_list
	// Then, switch away from it	
	int oldvalue = splx(HIGH);	
	if (!cur_running_thread->finished) {
		sthread_enqueue(ready_list, cur_running_thread);
	}
	switch_thread(cur_running_thread);

	// clean up any dead threads
	clean_dead_threads();

	splx(oldvalue);
}

void switch_thread(sthread_t old_thread) {
	if (!sthread_queue_is_empty(ready_list)) {
		sthread_t new_thread = sthread_dequeue(ready_list);
		cur_running_thread = new_thread;
		//printf("switched thread to thread (TID) = %i\n", cur_running_thread->tid);
		sthread_switch(old_thread->saved_ctx, cur_running_thread->saved_ctx);

	}
}

/* Add any new part 1 functions here */
void dummyFunc(void) {
	splx(LOW); // enable interupt for the user code 
	void* ret = cur_running_thread->start_routine(cur_running_thread->arg);
	sthread_user_exit(ret);
}

/* Cleans up any dead threads on dead thread queue */
void clean_dead_threads(void) {
	// int val = splx(HIGH);
	
	// cleans any dead threads in the dead queue.
	while (!sthread_queue_is_empty(dead_queue)) {
		sthread_t dead_thread = sthread_dequeue(dead_queue);
		debug_print("freeing dead thread, tid = %i \n", dead_thread->tid);
		sthread_free_ctx(dead_thread->saved_ctx);
		free(dead_thread->ret);
		free(dead_thread);
		while(atomic_test_and_set(&alive_thread_lock)) {};
		alive_thread--;
		atomic_clear(&alive_thread_lock);

		// If we clean the last "alive" thread, also clean the queue free list
		if (alive_thread == 0) {
			sthread_queue_clear_free_list();
			debug_print("%s\n", "cleaned the queue");

		}
	}
	// splx(val);
}


void preempt_func(void) {
	//sthread_user_yield();
	sthread_user_yield();

}

/*********************************************************************/
/* Part 2: Synchronization Primitives                                */
/*********************************************************************/

struct _sthread_mutex {
	/* Fill in mutex data structure */
	lock_t lock;
	sthread_queue_t blocked_threads_queue;
	lock_t blocked_queue_lock;
};

sthread_mutex_t sthread_user_mutex_init() {
	sthread_mutex_t mutex = malloc(sizeof(struct _sthread_mutex));
	mutex->lock = 0; // 0 is not locked; 1 is locked
	mutex->blocked_threads_queue = sthread_new_queue();
	mutex->blocked_queue_lock = 0;
	return mutex;
}

void sthread_user_mutex_free(sthread_mutex_t lock) {
	if (lock == NULL) {
		debug_print("%s\n", "sthread_user_mutex_free() freeing null lock\n");
		return;
	}

	if (!sthread_queue_is_empty(lock->blocked_threads_queue)) {
		debug_print("%s\n","Trying to free mutex that has blocked threads... returning\n");
		return;
	}

	sthread_free_queue(lock->blocked_threads_queue);
	free(lock);	
}

void sthread_user_mutex_lock(sthread_mutex_t lock) {

	if (lock == NULL) {
		debug_print("%s\n","sthread_user_mutex_lock() lock is null\n");
		return;
	}

	// if lock is locked, add the cur thread to blocked queue and switch away from it
	while(atomic_test_and_set(&lock->lock)) {
		while(atomic_test_and_set(&lock->blocked_queue_lock)){}
		sthread_enqueue(lock->blocked_threads_queue, cur_running_thread);
		atomic_clear(&lock->blocked_queue_lock);
		int old_value = splx(HIGH);
		switch_thread(cur_running_thread);
		splx(old_value);
	}

	// enable lock
	//lock->lock = 1;
	//atomic_test_and_set(&lock->lock);
	//printf("check lock\n");
}

void sthread_user_mutex_unlock(sthread_mutex_t lock) {
	if (lock == NULL) {
		debug_print("%s\n","sthread_user_mutex_unlock() lock is null\n");
		return;
	}

	// should put tid inside lock to prevent malicious unlock
	atomic_clear(&lock->lock);
	
	while(atomic_test_and_set(&lock->blocked_queue_lock)){}
	// switch to any threads that were in the blocked queue for the lock

	if (!sthread_queue_is_empty(lock->blocked_threads_queue)) {
		sthread_t blocked_thread = sthread_dequeue(lock->blocked_threads_queue);
		atomic_clear(&lock->blocked_queue_lock);

		int old = splx(HIGH);
		sthread_enqueue(ready_list, blocked_thread);
		splx(old);

		sthread_yield();

	} else {
		atomic_clear(&lock->blocked_queue_lock);
	}
}


struct _sthread_cond {
	/* Fill in condition variable structure */
	sthread_queue_t waiting_thread_queue;
	lock_t lock;
};

sthread_cond_t sthread_user_cond_init(void) {
	sthread_cond_t cond = malloc(sizeof(struct _sthread_cond));
	cond->waiting_thread_queue = sthread_new_queue();
	cond->lock = 0;
	return cond;
}

void sthread_user_cond_free(sthread_cond_t cond) {
	if (cond == NULL) {
		debug_print("%s\n","sthread_user_cond_free() trying to null condition variable; returning...\n");
		return;
	}

	sthread_free_queue(cond->waiting_thread_queue);
	free(cond);
}

void sthread_user_cond_signal(sthread_cond_t cond) {
	if (cond == NULL) {
		debug_print("%s\n","sthread_user_cond_signal() cond is null; returning...\n");
		return;
	}

	while(atomic_test_and_set(&cond->lock)){};

	// wake up only one thread, by moving it from waiting queue to ready queue
	if (!sthread_queue_is_empty(cond->waiting_thread_queue)) {
		sthread_t waiting_thread = sthread_dequeue(cond->waiting_thread_queue);
		atomic_clear(&cond->lock);

		int old = splx(HIGH);
		sthread_enqueue(ready_list, waiting_thread);
		splx(old);

	}
	else {
		debug_print("%s\n","sthread_user_cond_signal() no threads to signal\n");
		atomic_clear(&cond->lock);
	}
}

void sthread_user_cond_broadcast(sthread_cond_t cond) {
	if (cond == NULL) {
		debug_print("%s\n","sthread_user_cond_broadcast() cond is null; returning...\n");
		return;
	}

	while(atomic_test_and_set(&cond->lock)){};

	int old = splx(HIGH); // disable interrupts when inserting into the ready queue
	while (!sthread_queue_is_empty(cond->waiting_thread_queue)) {
		sthread_t waiting_thread = sthread_dequeue(cond->waiting_thread_queue);
		atomic_test_and_set(&cond->lock);
		sthread_enqueue(ready_list, waiting_thread);
	}
	splx(old);

	atomic_clear(&cond->lock);

}

void sthread_user_cond_wait(sthread_cond_t cond,
		sthread_mutex_t lock) {

	if (cond == NULL || lock == NULL) {
		debug_print("%s\n","sthread_user_cond_wait() cond or lock is null; returning...\n");
		return;
	}

	sthread_user_mutex_unlock(lock);

	atomic_test_and_set(&cond->lock);
	sthread_enqueue(cond->waiting_thread_queue, cur_running_thread);
	atomic_clear(&cond->lock);

	int old_value = splx(HIGH);
	switch_thread(cur_running_thread);
	splx(old_value);

	sthread_user_mutex_lock(lock);
}
