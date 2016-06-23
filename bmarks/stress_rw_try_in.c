/*
 * File: test_correct.c
 *
 * Description: 
 *      Test which exposes bugs in lock algorithms;
 *      By no means an exhaustive test, but generally exposes
 *      a buggy algorithm;
 *      Each thread continuously increments a global counter
 *      protected by a lock; if the final counter value is not
 *      equal to the sum of the increments by each thread, then
 *      the lock algorithm has a bug.
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdint.h>
#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#ifndef __sparc__
#  include <numa.h>
#endif

//#define LOCK_IN TAS
#include "lock_in.h"

#define XSTR(s) #s

//number of concurrent threads
#define DEFAULT_NUM_THREADS 1
//total duration of the test, in milliseconds
#define DEFAULT_DURATION 1000

static volatile int stop;

__thread unsigned long* seeds;
__thread uint32_t phys_id;
__thread uint32_t cluster_id;

typedef struct shared_data{
    volatile uint64_t counter;
    char padding[56];
} shared_data;

volatile shared_data* protected_data;
int duration;
int num_threads;

typedef struct barrier {
    pthread_cond_t complete;
    pthread_mutex_t mutex;
    int count;
    int crossing;
} barrier_t;

void barrier_init(barrier_t *b, int n)
{
    pthread_cond_init(&b->complete, NULL);
    pthread_mutex_init(&b->mutex, NULL);
    b->count = n;
    b->crossing = 0;
}

void barrier_cross(barrier_t *b)
{
    pthread_mutex_lock(&b->mutex);
    /* One more thread through */
    b->crossing++;
    /* If not all here, wait */
    if (b->crossing < b->count) {
        pthread_cond_wait(&b->complete, &b->mutex);
    } else {
        pthread_cond_broadcast(&b->complete);
        /* Reset for next time */
        b->crossing = 0;
    }
    pthread_mutex_unlock(&b->mutex);
}

pthread_rwlock_t lock = PTHREAD_RWLOCK_INITIALIZER;

typedef struct thread_data {
    union
    {
        struct
        {
            barrier_t *barrier;
            unsigned long num_wacquires;
            unsigned long num_racquires;
            int id;
        };
        char padding[CACHE_LINE_SIZE];
    };
} thread_data_t;

#define ARRAY_LEN 32
volatile int array[ARRAY_LEN] = { 0 };

void* 
test_correctness(void *data)
{
  thread_data_t *d = (thread_data_t *)data;

  barrier_cross(d->barrier);

  size_t iter = 0;

  while (stop == 0) 
    {
      if (iter++ & 15)
	{
	  if (!pthread_rwlock_tryrdlock(&lock))
	    {
	      int i, first = array[0];
	      for (i = 1; i < ARRAY_LEN; i++)
		{
		  if (array[i] != first)
		    {
		      printf("*** error\n");
		    }
		}
	      pthread_rwlock_unlock(&lock);
	      d->num_racquires++;
	    }
	}
      else
	{
	  if (!pthread_rwlock_trywrlock(&lock))
	    {
	      protected_data->counter++;
	      int i, val = protected_data->counter;
	      for (i = ARRAY_LEN - 1; i >= 0; i--)
		{
		  array[i] = val;
		}
	      pthread_rwlock_unlock(&lock);
	      d->num_wacquires++;
	    }
	}
    }

  return NULL;
}


void catcher(int sig)
{
    static int nb = 0;
    printf("CAUGHT SIGNAL %d\n", sig);
    if (++nb >= 3)
        exit(1);
}


int main(int argc, char **argv)
{
  struct option long_options[] = {
    // These options don't set a flag
    {"help",                      no_argument,       NULL, 'h'},
    {"duration",                  required_argument, NULL, 'd'},
    {"num-threads",               required_argument, NULL, 'n'},
    {NULL, 0, NULL, 0}
  };

  int i, c;
  thread_data_t *data;
  pthread_t *threads;
  pthread_attr_t attr;
  barrier_t barrier;
  struct timeval start, end;
  struct timespec timeout;
  duration = DEFAULT_DURATION;
  num_threads = DEFAULT_NUM_THREADS;
  sigset_t block_set;

  while(1) {
    i = 0;
    c = getopt_long(argc, argv, "h:d:n:", long_options, &i);

    if(c == -1)
      break;

    if(c == 0 && long_options[i].flag == 0)
      c = long_options[i].val;

    switch(c) {
    case 0:
      /* Flag is automatically set */
      break;
    case 'h':
      printf("lock stress test\n"
	     "\n"
	     "Usage:\n"
	     "  stress_test [options...]\n"
	     "\n"
	     "Options:\n"
	     "  -h, --help\n"
	     "        Print this message\n"
	     "  -d, --duration <int>\n"
	     "        Test duration in milliseconds (0=infinite, default=" XSTR(DEFAULT_DURATION) ")\n"
	     "  -n, --num-threads <int>\n"
	     "        Number of threads (default=" XSTR(DEFAULT_NUM_THREADS) ")\n"
	     );
      exit(0);
    case 'd':
      duration = atoi(optarg);
      break;
    case 'n':
      num_threads = atoi(optarg);
      break;
    case '?':
      printf("Use -h or --help for help\n");
      exit(0);
    default:
      exit(1);
    }
  }
  assert(duration >= 0);
  assert(num_threads > 0);

  protected_data = (shared_data*) malloc(sizeof(shared_data));
  protected_data->counter=0;
#ifdef PRINT_OUTPUT
  printf("Duration               : %d\n", duration);
  printf("Number of threads      : %d\n", num_threads);
#endif
  timeout.tv_sec = duration / 1000;
  timeout.tv_nsec = (duration % 1000) * 1000000;

  if ((data = (thread_data_t *)malloc(num_threads * sizeof(thread_data_t))) == NULL) {
    perror("malloc");
    exit(1);
  }
  if ((threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t))) == NULL) {
    perror("malloc");
    exit(1);
  }

  stop = 0;
  /* Init locks */
#ifdef PRINT_OUTPUT
  printf("Initializing locks\n");
#endif
  /* Access set from all threads */
  barrier_init(&barrier, num_threads + 1);
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  for (i = 0; i < num_threads; i++) {
#ifdef PRINT_OUTPUT
    printf("Creating thread %d\n", i);
#endif
    data[i].id = i;
    data[i].num_racquires = 0;
    data[i].num_wacquires = 0;
    data[i].barrier = &barrier;
    if (pthread_create(&threads[i], &attr, test_correctness, (void *)(&data[i])) != 0) {
      fprintf(stderr, "Error creating thread\n");
      exit(1);
    }
  }
  pthread_attr_destroy(&attr);

  /* Catch some signals */
  if (signal(SIGHUP, catcher) == SIG_ERR ||
      signal(SIGINT, catcher) == SIG_ERR ||
      signal(SIGTERM, catcher) == SIG_ERR) {
    perror("signal");
    exit(1);
  }

  /* Start threads */
  barrier_cross(&barrier);

#ifdef PRINT_OUTPUT
  printf("STARTING...\n");
#endif
  gettimeofday(&start, NULL);
  if (duration > 0) {
    nanosleep(&timeout, NULL);
  } else {
    sigemptyset(&block_set);
    sigsuspend(&block_set);
  }
  stop = 1;

  gettimeofday(&end, NULL);
#ifdef PRINT_OUTPUT
  printf("STOPPING...\n");
#endif
  /* Wait for thread completion */
  for (i = 0; i < num_threads; i++) {
    if (pthread_join(threads[i], NULL) != 0) {
      fprintf(stderr, "Error waiting for thread completion\n");
      exit(1);
    }
  }

  duration = (end.tv_sec * 1000 + end.tv_usec / 1000) - (start.tv_sec * 1000 + start.tv_usec / 1000);

  uint64_t acquires = 0, racquires = 0;
  for (i = 0; i < num_threads; i++) {
#ifdef PRINT_OUTPUT
    printf("Thread %d\n", i);
    printf("  #acquire-w : %lu\n", data[i].num_wacquires);
    printf("  #acquire-r : %lu\n", data[i].num_racquires);
#endif
    acquires += data[i].num_wacquires;
    racquires += data[i].num_racquires;
  }
#ifdef PRINT_OUTPUT
  printf("Duration      : %d (ms)\n", duration);
#endif
  printf("Counter total : %llu, Expected: %llu\n", (unsigned long long) protected_data->counter, (unsigned long long) acquires);
  printf("Plus, read acq: %llu\n", (unsigned long long) racquires);
  if (protected_data->counter != acquires) {
    printf("Incorrect lock behavior!\n");
  }

  free(threads);
  free(data);

  return 0;
}
