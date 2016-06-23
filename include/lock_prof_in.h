/*
 * File: lock_prof_in.h
 * Author: Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Vasileios Trigonakis
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


#ifndef _LOCK_PROF_IN_H_
#define _LOCK_PROF_IN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/futex.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <fcntl.h>
#include <pthread.h>
#include <malloc.h>
#include <limits.h>

#if !defined(__x86_64__)
#  error This file is designed to work only on x86_64 architectures! 
#endif

#define LOCK_IN_NAME "LOCK-PROFILER"

/* ******************************************************************************** */
/* settings *********************************************************************** */
#if !defined(USE_FUTEX_COND)
#  define USE_FUTEX_COND 1	/* use futex-based (1) or spin-based futexs */
#endif
#if !defined(PADDING)
#  define PADDING        1      /* padd locks/conditionals to cache-line */
#endif

#define FREQ_CPU_GHZ     2.8	/* core frequency in GHz */
#define REPLACE_MUTEX    1	/* ovewrite the pthread_[mutex|cond] functions */

#define LOCK_PROF_PRINT_EVERY ((1<<14)-1)
#define LOCK_PROF_LATNC_EVERY 1023

#define LOCK_PROF_MACRO  1

/* ******************************************************************************** */

#define CACHE_LINE_SIZE 64

#if !defined(PAUSE_IN)
#  define PAUSE_IN()			\
  ;
#endif

typedef struct lock_prof_lock 
{
  volatile uint32_t head;
  volatile uint32_t tail;

  size_t n_spins;
  size_t n_queue;
  size_t n_lat;
  uint64_t s_lat;
  uint64_t t_lat;

#if PADDING == 1
  uint8_t padding[CACHE_LINE_SIZE - 12 * sizeof(uint32_t)];
#endif
} lock_prof_lock_t;

#define LOCK_PROF_LOCK_INITIALIZER { .head = 1, .tail = 0 }

typedef struct lock_prof_cond
{
  uint32_t lock_prof;
  volatile uint32_t head;
  lock_prof_lock_t* l;
#if PADDING == 1
    uint8_t padding[CACHE_LINE_SIZE - 16];
#endif
} lock_prof_cond_t;

#define LOCK_PROF_COND_INITIALIZER { 0, 0, NULL }


static inline int
lock_prof_lock_trylock(lock_prof_lock_t* lock) 
{
  uint32_t to = lock->tail;
  if (lock->head - to == 1)
    {
      return (__sync_val_compare_and_swap(&lock->tail, to, to + 1) != to);
    }

  return 1;
}

static inline uint64_t
lock_prof_getticks(void)
{
  unsigned hi, lo;
  asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
  return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}


    /* if (__builtin_expect((((lock)->tail+1) & LOCK_PROF_LATNC_EVERY) == 0, 0)) \ */
    /*   {									\ */
    /* 	__s = lock_prof_getticks();					\ */
    /* 	(lock)->s_lat = __s;						\ */
    /*   }									\ */


#if LOCK_PROF_MACRO == 1

#define lock_prof_lock_lock(lock)					\
  ({									\
    volatile uint64_t __s = 0;						\
    uint32_t ticket = __sync_add_and_fetch(&((lock)->tail), 1);		\
    if (__builtin_expect((ticket & LOCK_PROF_PRINT_EVERY) == 0, 0))	\
      {									\
	size_t n_acq = (lock)->head;					\
	printf("[lock stats] avg spin: %-10zu | avg queue: %-5.2f | avg lat: %-10zu @ (%p:%s:%s:%d)m\n", \
	       (lock)->n_spins / n_acq,					\
	       (double) (lock)->n_queue / n_acq,			\
	       (lock)->t_lat /(lock)->n_lat,				\
	       lock, __FILE__, __FUNCTION__, __LINE__			\
	       );							\
      }									\
    if (__builtin_expect((ticket & LOCK_PROF_LATNC_EVERY) == 0, 0))	\
      {									\
	__s = lock_prof_getticks();					\
	(lock)->s_lat = __s;						\
      }									\
    (uint32_t) __lock_prof_lock_lock((lock), ticket);			\
    0;									\
  })


static inline int
__lock_prof_lock_lock(lock_prof_lock_t* lock, const uint32_t ticket) 
{
  int distance, once = 1;
  size_t spins = 0;
  do
    {
      distance = ticket - lock->head;
      if (distance == 0)
  	{
	  if (__builtin_expect((ticket & LOCK_PROF_LATNC_EVERY) == 0, 0))
	    {
	      uint64_t __e = lock_prof_getticks();
	      uint64_t __l = __e - lock->s_lat;
	      lock->t_lat += __l;
	      lock->n_lat++;
	    }
	  lock->n_spins += spins;
  	  return 0;
  	}

      spins++;

      if (once)
	{
	  once = 0;
	  (size_t) __sync_fetch_and_add(&lock->n_queue, distance);
	}
    }
  while (1);
  return 0;
}

#else  /* !MACRO */

  static inline int
  lock_prof_lock_lock(lock_prof_lock_t* lock) 
  {
    uint32_t ticket = __sync_add_and_fetch(&((lock)->tail), 1);		
    if (__builtin_expect((ticket & LOCK_PROF_PRINT_EVERY) == 0, 0))	
      {									
	size_t n_acq = (lock)->head;					
	printf("[lock stats] avg spin: %-10zu | avg queue: %-5.2f | avg lat: %-10zu @ (%p:%s:%s:%d)\n", 
	       (lock)->n_spins / n_acq,					
	       (double) (lock)->n_queue / n_acq,			
	       (lock)->t_lat /(lock)->n_lat,				
	       lock, __FILE__, __FUNCTION__, __LINE__				
	       );
      }									
    if (__builtin_expect((ticket & LOCK_PROF_LATNC_EVERY) == 0, 0))	
      {									
	uint64_t __s = lock_prof_getticks();					
	(lock)->s_lat = __s;						
      }									

    int distance, once = 1;
    size_t spins = 0;
    do
      {
	distance = ticket - lock->head;
	if (distance == 0)
	  {
	    if (__builtin_expect((ticket & LOCK_PROF_LATNC_EVERY) == 0, 0))
	      {
		uint64_t __e = lock_prof_getticks();
		uint64_t __l = __e - lock->s_lat;
		lock->t_lat += __l;
		lock->n_lat++;
	      }
	    lock->n_spins += spins;
	    return 0;
	  }

	spins++;

	if (once)
	  {
	    once = 0;
	    (size_t) __sync_fetch_and_add(&lock->n_queue, distance);
	  }
      }
    while (1);
    return 0;
  }

#endif

static inline int
lock_prof_lock_unlock(lock_prof_lock_t* lock) 
{
#ifdef __tile__
  MEM_BARRIER;
#endif
  asm volatile("" ::: "memory");
  if (__builtin_expect((lock->tail >= lock->head), 1)) 
    {
      lock->head++;
    }
  return 0;
}


static inline int
lock_prof_lock_init(lock_prof_lock_t* the_lock, const pthread_mutexattr_t* a) 
{
    the_lock->head=1;
    the_lock->tail=0;
    asm volatile ("mfence");
    return 0;
}

static inline int
lock_prof_lock_destroy(lock_prof_lock_t* the_lock) 
{
    return 0;
}


#if USE_FUTEX_COND == 1

static inline int
sys_futex(void* addr1, int op, int val1, struct timespec* timeout, void* addr2, int val3)
{
  return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}

static inline int
lock_prof_cond_wait(lock_prof_cond_t* c, lock_prof_lock_t* m)
{
  int head = c->head;

  if (c->l != m)
    {
      if (c->l) return EINVAL;
      
      /* Atomically set mutex inside cv */
      __attribute__ ((unused)) int dummy = (uintptr_t) __sync_val_compare_and_swap(&c->l, NULL, m);
      if (c->l != m) return EINVAL;
    }
  
  lock_prof_lock_unlock(m);
  
  sys_futex((void*) &c->head, FUTEX_WAIT_PRIVATE, head, NULL, NULL, 0);
  
  lock_prof_lock_lock(m);
  
  return 0;
}

static inline int
lock_prof_cond_timedwait(lock_prof_cond_t* c, lock_prof_lock_t* m, const struct timespec* ts)
{
  int ret = 0;
  int head = c->head;

  if (c->l != m)
    {
      if (c->l) return EINVAL;
      
      /* Atomically set mutex inside cv */
      __attribute__ ((unused)) int dummy = (uintptr_t) __sync_val_compare_and_swap(&c->l, NULL, m);
      if (c->l != m) return EINVAL;
    }
  
  lock_prof_lock_unlock(m);
  
  struct timespec rt;
  /* Get the current time.  So far we support only one clock.  */
  struct timeval tv;
  (void) gettimeofday (&tv, NULL);

  /* Convert the absolute timeout value to a relative timeout.  */
  rt.tv_sec = ts->tv_sec - tv.tv_sec;
  rt.tv_nsec = ts->tv_nsec - tv.tv_usec * 1000;
  
  if (rt.tv_nsec < 0)
    {
      rt.tv_nsec += 1000000000;
      --rt.tv_sec;
    }
  /* Did we already time out?  */
  if (__builtin_expect (rt.tv_sec < 0, 0))
    {
      ret = ETIMEDOUT;
      goto timeout;
    }

  sys_futex((void*) &c->head, FUTEX_WAIT_PRIVATE, head, &rt, NULL, 0);
  
  (void) gettimeofday (&tv, NULL);
  rt.tv_sec = ts->tv_sec - tv.tv_sec;
  rt.tv_nsec = ts->tv_nsec - tv.tv_usec * 1000;
  if (rt.tv_nsec < 0)
    {
      rt.tv_nsec += 1000000000;
      --rt.tv_sec;
    }

  if (rt.tv_sec < 0)
    {
      ret = ETIMEDOUT;
    }

 timeout:
  lock_prof_lock_lock(m);
  
  return ret;
}

static inline int
lock_prof_cond_init(lock_prof_cond_t* c, const pthread_condattr_t* a)
{
  (void) a;
  
  c->l = NULL;
  
  /* Sequence variable doesn't actually matter, but keep valgrind happy */
  c->head = 0;
  
  return 0;
}

static inline int 
lock_prof_cond_destroy(lock_prof_cond_t* c)
{
  /* No need to do anything */
  (void) c;
  return 0;
}

static inline int
lock_prof_cond_signal(lock_prof_cond_t* c)
{
  /* We are waking someone up */
  __sync_add_and_fetch(&c->head, 1);
  
  /* Wake up a thread */
  sys_futex((void*) &c->head, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
  
  return 0;
}

static inline int
lock_prof_cond_broadcast(lock_prof_cond_t* c)
{
  lock_prof_lock_t* m = c->l;
  
  /* No mutex means that there are no waiters */
  if (!m) return 0;
  
  /* We are waking everyone up */
  __sync_add_and_fetch(&c->head, 1);
  
  /* Wake one thread, and requeue the rest on the mutex */
  sys_futex((void*) &c->head, FUTEX_REQUEUE_PRIVATE, INT_MAX, NULL, NULL, 0);
  
  return 0;
}

#else

static inline int
lock_prof_cond_init(lock_prof_cond_t* c, pthread_condattr_t* a)
{
  c->head = 0;
  c->lock_prof = 0;
  c->l = NULL;
  return 0;
}

static inline int
lock_prof_cond_destroy(lock_prof_cond_t* c)
{
  c->head = -1;
  return 1;
}

static inline int
lock_prof_cond_signal(lock_prof_cond_t* c)
{
  if (c->lock_prof > c->head)
    {
      __sync_fetch_and_add(&c->head, 1);
    }
  return 1;
}

static inline int 
lock_prof_cond_broadcast(lock_prof_cond_t* c)
{
  if (c->lock_prof == c->head)
    {
      return 0;
    }

  uint32_t ch, ct;
  do
    {
      ch = c->head;
      ct = c->lock_prof;
    }
  while (__sync_val_compare_and_swap(&c->head, ch, ct) != ch);

  return 1;
}

static inline int
lock_prof_cond_wait(lock_prof_cond_t* c, lock_prof_lock_t* l)
{
  uint32_t cond_lock_prof = ++c->lock_prof;

  if (c->l != l)
    {
      if (c->l)
	{
	  return EINVAL;
	}
      __attribute__ ((unused)) void* dummy = __sync_val_compare_and_swap(&c->l, NULL, l);
      if (c->l != l)
	{
	  return EINVAL;
	}
    }

  lock_prof_lock_unlock(l);

  while (1)
    {
      int distance = cond_lock_prof - c->head;
      if (distance == 0)
	{
	  break;
	}
      PAUSE_IN();
    }

  lock_prof_lock_lock(l);
  return 1;
}

static inline int
lock_prof_cond_timedwait(lock_prof_cond_t* c, lock_prof_lock_t* l, const struct timespec* ts)
{
  int ret = 0;

  uint32_t cond_lock_prof = ++c->lock_prof;

  if (c->l != l)
    {
      if (c->l)
	{
	  return EINVAL;
	}
      __attribute__ ((unused)) void* dummy = __sync_val_compare_and_swap(&c->l, NULL, l);
      if (c->l != l)
	{
	  return EINVAL;
	}
    }

  lock_prof_lock_unlock(l);

  struct timespec rt;
  /* Get the current time.  So far we support only one clock.  */
  struct timeval tv;
  (void) gettimeofday (&tv, NULL);

  /* Convert the absolute timeout value to a relative timeout.  */
  rt.tv_sec = ts->tv_sec - tv.tv_sec;
  rt.tv_nsec = ts->tv_nsec - tv.tv_usec * 1000;
  
  if (rt.tv_nsec < 0)
    {
      rt.tv_nsec += 1000000000;
      --rt.tv_sec;
    }

  size_t nanos = 1000000000 * rt.tv_sec + rt.tv_nsec;
  uint64_t ticks = FREQ_CPU_GHZ * nanos;
  uint64_t to = lock_prof_getticks() + ticks;

  while (cond_lock_prof > c->head)
    {
      PAUSE_IN();
      if (lock_prof_getticks() > to)
	{
	  ret = ETIMEDOUT;
	  break;
	}
    }

  lock_prof_lock_lock(l);
  return ret;
}

#endif	/* USE_FUTEX_COND */

static inline int
lock_prof_lock_timedlock(lock_prof_lock_t* l, const struct timespec* ts)
{
  fprintf(stderr, "** warning -- pthread_mutex_timedlock not implemented\n");
  return 0;
}

#if REPLACE_MUTEX == 1
#  define pthread_mutex_init    lock_prof_lock_init
#  define pthread_mutex_destroy lock_prof_lock_destroy
#  define pthread_mutex_lock    lock_prof_lock_lock
#  define pthread_mutex_timedlock lock_prof_lock_timedlock
#  define pthread_mutex_unlock  lock_prof_lock_unlock
#  define pthread_mutex_trylock lock_prof_lock_trylock
#  define pthread_mutex_t       lock_prof_lock_t
#  undef  PTHREAD_MUTEX_INITIALIZER
#  define PTHREAD_MUTEX_INITIALIZER LOCK_PROF_LOCK_INITIALIZER

#  define pthread_cond_init     lock_prof_cond_init
#  define pthread_cond_destroy  lock_prof_cond_destroy
#  define pthread_cond_signal   lock_prof_cond_signal
#  define pthread_cond_broadcast lock_prof_cond_broadcast
#  define pthread_cond_wait     lock_prof_cond_wait
#  define pthread_cond_timedwait lock_prof_cond_timedwait
#  define pthread_cond_t        lock_prof_cond_t
#  undef  PTHREAD_COND_INITIALIZER
#  define PTHREAD_COND_INITIALIZER LOCK_PROF_COND_INITIALIZER
#endif

#ifdef __cplusplus
}
#endif

#endif


