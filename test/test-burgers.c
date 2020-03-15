#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <sthread.h>

static int burger_available = 0;
static int burger_produced = 0;
static int burger_needed = 0;
static int burger_consumed = 0;
sthread_mutex_t mutex;
sthread_cond_t cond;

void* produce_burger(void *arg);
void* consume_burger(void *arg);

int main(int argc, char **argv) {
  if (argc != 4) {
    printf("This program requires 3 arguments, N cooks, M students and K burgers\n");
    exit(1);
  }
  int cooks = atoi(argv[1]);
  int students = atoi(argv[2]);
  burger_needed = atoi(argv[3]);
  
  sthread_t cooks_threads[cooks];
  sthread_t students_threads[students];
  
  mutex = sthread_mutex_init();
  cond = sthread_cond_init();
  
  sthread_init();
  
  for(int i = 0; i < cooks; i++) {
    cooks_threads[i] = sthread_create(produce_burger, NULL, 1); 
    if (cooks_threads[i] == NULL) {
      printf("sthread_create failed\n");
      exit(1);
    }
  }
  
  for(int i = 0; i < students; i++) {
    students_threads[i] = sthread_create(consume_burger, NULL, 1);
    if (students_threads[i] == NULL) {
      printf("sthread_create failed\n");
      exit(1);
    }
  }
  
  
  for(int i = 0; i < cooks; i++) {
    sthread_join(cooks_threads[i]);
  }
  
  for(int i = 0; i < students; i++) {
    sthread_join(students_threads[i]);
  }
  sthread_mutex_free(mutex);
  sthread_cond_free(cond);
  exit(1);
}

void* produce_burger(void *arg) {
  while (true) {
    sthread_mutex_lock(mutex);
    if (burger_produced == burger_needed) { 
      // if enough burgers are produced, unlock and break
      sthread_cond_broadcast(cond);
      sthread_mutex_unlock(mutex);
      break;
    }
    // increments burgers produced and avaible and signal the condition variable
    burger_produced++;
    burger_available++;
    printf("burger %d is produced\n", burger_produced);
    sthread_cond_signal(cond);
    sthread_mutex_unlock(mutex);
    sthread_yield();
  }
  return arg;
}

void* consume_burger(void *arg) {
  while (true) {
    sthread_mutex_lock(mutex);
    if (burger_consumed == burger_needed) {
      // if enough burgers are consumed, unlock and break
      sthread_mutex_unlock(mutex);
      break;
    }
    if (burger_available == 0) {
      // wait if no burgers are avaible
      sthread_cond_wait(cond, mutex);
      sthread_mutex_unlock(mutex);
      continue;
    }
    // decrement burgers avaible and increment consumed
    burger_available--;
    burger_consumed ++;
    printf("burger %d is consumed\n", burger_consumed);
    sthread_mutex_unlock(mutex);
    sthread_yield();
  }
  return arg;
}

