/*
 * File: ladap_in.h
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


#ifndef _LADAP_IN_H_
#define _LADAP_IN_H_

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

#define LADAP_FASTER      1
#define LADAP_PRINT_EVERY ((2<<(20-LADAP_FASTER))-1)
#define LADAP_LATNC_EVERY 1023
#define LADAP_REP_TRAININ ((2<<(24-LADAP_FASTER))-1)

/* ******************************************************************************** */

#define CACHE_LINE_SIZE 64

#if !defined(PAUSE_IN)
#  define PAUSE_IN()			\
  ;
#endif

typedef struct ladap_lock 
{
  volatile uint32_t head;
  volatile uint32_t tail;

  size_t n_spins;
  size_t n_queue;
  size_t n_lat;
  uint64_t s_lat;
  uint64_t t_lat;
  uint64_t n_yields;
  uint32_t do_spins;

#if PADDING == 1
  uint8_t padding[CACHE_LINE_SIZE - 15 * sizeof(uint32_t)];
#endif
} ladap_lock_t;

#define LADAP_LOCK_INITIALIZER { .head = 1, .tail = 0 }

typedef struct ladap_cond
{
  uint32_t ladap;
  volatile uint32_t head;
  ladap_lock_t* l;
#if PADDING == 1
    uint8_t padding[CACHE_LINE_SIZE - 16];
#endif
} ladap_cond_t;

#define LADAP_COND_INITIALIZER { 0, 0, NULL }


static inline int
ladap_lock_trylock(ladap_lock_t* lock) 
{
  uint32_t to = lock->tail;
  if (lock->head - to == 1)
    {
      return (__sync_val_compare_and_swap(&lock->tail, to, to + 1) != to);
    }

  return 1;
}

static inline uint64_t
ladap_getticks(void)
{
  unsigned hi, lo;
  asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
  return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}


    /* if (__builtin_expect((((lock)->tail+1) & LADAP_LATNC_EVERY) == 0, 0)) \ */
    /*   {									\ */
    /* 	__s = ladap_getticks();					\ */
    /* 	(lock)->s_lat = __s;						\ */
    /*   }									\ */


#define ladap_lock_lock(lock)					\
  ({									\
    volatile uint64_t __s = 0;						\
    uint32_t ticket = __sync_add_and_fetch(&((lock)->tail), 1);		\
    if (__builtin_expect((ticket & LADAP_PRINT_EVERY) == 0, 0))	\
      {									\
	size_t n_acq = (lock)->head;					\
	printf("[lock stats] avg spin: %-8zu | avg queue: %-5.2f |"	\
	       "avg lat: %-7zu | yld/acq: %-5.2f @ (%s:%s:%d)\n",	\
	       (lock)->n_spins / n_acq,					\
	       (double) (lock)->n_queue / n_acq,			\
	       (lock)->t_lat /(lock)->n_lat,				\
	       n_acq / (double) (lock)->n_yields,			\
	       __FILE__, __FUNCTION__, __LINE__				\
	       );							\
      }									\
    if (__builtin_expect((ticket & LADAP_LATNC_EVERY) == 0, 0))	\
      {									\
	__s = ladap_getticks();					\
	(lock)->s_lat = __s;						\
      }									\
    (uint32_t) __ladap_lock_lock((lock), ticket);			\
    0;									\
  })


static inline int
__ladap_lock_lock(ladap_lock_t* lock, const uint32_t ticket) 
{
  int distance, once = 1;
  size_t spins = 0;
  do
    {
      distance = ticket - lock->head;
      if (distance == 0)
  	{
	  if (__builtin_expect((ticket & LADAP_LATNC_EVERY) == 0, 0))
	    {
	      uint64_t __e = ladap_getticks();
	      uint64_t __l = __e - lock->s_lat;
	      lock->t_lat += __l;
	      lock->n_lat++;
	      if (__builtin_expect((ticket & LADAP_REP_TRAININ) == 0, 0))
		{
		  size_t spin_limit = lock->n_spins / lock->head;
		  printf("[lock train] yield after #spins: %zu\n",
			 spin_limit);
		  lock->do_spins = spin_limit;
		}
	    }
	  lock->n_spins += spins;
  	  return 0;
  	}

      if (__builtin_expect((++spins == lock->do_spins), 0))
	{
	  sched_yield();
	  lock->n_yields++;
	}

	if (__builtin_expect(once, 0))
	{
	  once = 0;
	  (size_t) __sync_fetch_and_add(&lock->n_queue, distance);
	}
    }
  while (1);
  return 0;
}

static inline int
ladap_lock_unlock(ladap_lock_t* lock) 
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
ladap_lock_init(ladap_lock_t* the_lock, const pthread_mutexattr_t* a) 
{
    the_lock->head=1;
    the_lock->tail=0;
    asm volatile ("mfence");
    return 0;
}

static inline int
ladap_lock_destroy(ladap_lock_t* the_lock) 
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
ladap_cond_wait(ladap_cond_t* c, ladap_lock_t* m)
{
  int head = c->head;

  if (c->l != m)
    {
      if (c->l) return EINVAL;
      
      /* Atomically set mutex inside cv */
      __attribute__ ((unused)) int dummy = (uintptr_t) __sync_val_compare_and_swap(&c->l, NULL, m);
      if (c->l != m) return EINVAL;
    }
  
  ladap_lock_unlock(m);
  
  sys_futex((void*) &c->head, FUTEX_WAIT_PRIVATE, head, NULL, NULL, 0);
  
  ladap_lock_lock(m);
  
  return 0;
}

static inline int
ladap_cond_timedwait(ladap_cond_t* c, ladap_lock_t* m, const struct timespec* ts)
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
  
  ladap_lock_unlock(m);
  
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
  ladap_lock_lock(m);
  
  return ret;
}

static inline int
ladap_cond_init(ladap_cond_t* c, const pthread_condattr_t* a)
{
  (void) a;
  
  c->l = NULL;
  
  /* Sequence variable doesn't actually matter, but keep valgrind happy */
  c->head = 0;
  
  return 0;
}

static inline int 
ladap_cond_destroy(ladap_cond_t* c)
{
  /* No need to do anything */
  (void) c;
  return 0;
}

static inline int
ladap_cond_signal(ladap_cond_t* c)
{
  /* We are waking someone up */
  __sync_add_and_fetch(&c->head, 1);
  
  /* Wake up a thread */
  sys_futex((void*) &c->head, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
  
  return 0;
}

static inline int
ladap_cond_broadcast(ladap_cond_t* c)
{
  ladap_lock_t* m = c->l;
  
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
ladap_cond_init(ladap_cond_t* c, pthread_condattr_t* a)
{
  c->head = 0;
  c->ladap = 0;
  c->l = NULL;
  return 0;
}

static inline int
ladap_cond_destroy(ladap_cond_t* c)
{
  c->head = -1;
  return 1;
}

static inline int
ladap_cond_signal(ladap_cond_t* c)
{
  if (c->ladap > c->head)
    {
      __sync_fetch_and_add(&c->head, 1);
    }
  return 1;
}

static inline int 
ladap_cond_broadcast(ladap_cond_t* c)
{
  if (c->ladap == c->head)
    {
      return 0;
    }

  uint32_t ch, ct;
  do
    {
      ch = c->head;
      ct = c->ladap;
    }
  while (__sync_val_compare_and_swap(&c->head, ch, ct) != ch);

  return 1;
}

static inline int
ladap_cond_wait(ladap_cond_t* c, ladap_lock_t* l)
{
  uint32_t cond_ladap = ++c->ladap;

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

  ladap_lock_unlock(l);

  while (1)
    {
      int distance = cond_ladap - c->head;
      if (distance == 0)
	{
	  break;
	}
      PAUSE_IN();
    }

  ladap_lock_lock(l);
  return 1;
}

static inline int
ladap_cond_timedwait(ladap_cond_t* c, ladap_lock_t* l, const struct timespec* ts)
{
  int ret = 0;

  uint32_t cond_ladap = ++c->ladap;

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

  ladap_lock_unlock(l);

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
  uint64_t to = ladap_getticks() + ticks;

  while (cond_ladap > c->head)
    {
      PAUSE_IN();
      if (ladap_getticks() > to)
	{
	  ret = ETIMEDOUT;
	  break;
	}
    }

  ladap_lock_lock(l);
  return ret;
}

#endif	/* USE_FUTEX_COND */

static inline int
ladap_lock_timedlock(ladap_lock_t* l, const struct timespec* ts)
{
  fprintf(stderr, "** warning -- pthread_mutex_timedlock not implemented\n");
  return 0;
}

#if REPLACE_MUTEX == 1
#  define pthread_mutex_init    ladap_lock_init
#  define pthread_mutex_destroy ladap_lock_destroy
#  define pthread_mutex_lock    ladap_lock_lock
#  define pthread_mutex_timedlock ladap_lock_timedlock
#  define pthread_mutex_unlock  ladap_lock_unlock
#  define pthread_mutex_trylock ladap_lock_trylock
#  define pthread_mutex_t       ladap_lock_t
#  undef  PTHREAD_MUTEX_INITIALIZER
#  define PTHREAD_MUTEX_INITIALIZER LADAP_LOCK_INITIALIZER

#  define pthread_cond_init     ladap_cond_init
#  define pthread_cond_destroy  ladap_cond_destroy
#  define pthread_cond_signal   ladap_cond_signal
#  define pthread_cond_broadcast ladap_cond_broadcast
#  define pthread_cond_wait     ladap_cond_wait
#  define pthread_cond_timedwait ladap_cond_timedwait
#  define pthread_cond_t        ladap_cond_t
#  undef  PTHREAD_COND_INITIALIZER
#  define PTHREAD_COND_INITIALIZER LADAP_COND_INITIALIZER
#endif

#ifdef __cplusplus
}
#endif

#endif


