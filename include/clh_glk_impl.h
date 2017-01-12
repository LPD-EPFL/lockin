/*
 * File: clh_glk_impl.h
 * Author: Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description:
 *     MCS implementation for MCS.
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Vasileios Trigonakis
 *               Distributed Programming Lab (LPD), EPFL
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


#ifndef _CLH_IN_H_
#define _CLH_IN_H_

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
#include <assert.h>

#if !defined(__x86_64__)
#  error This file is designed to work only on x86_64 architectures! 
#endif

#define LOCK_IN_NAME "CLH"

/* ******************************************************************************** */
/* settings *********************************************************************** */
#define USE_FUTEX_COND 1	/* use futex-based (1) or spin-based futexs */
#define PADDING        1        /* padd locks/conditionals to cache-line */
#define FREQ_CPU_GHZ   2.8	/* core frequency in GHz */
#define REPLACE_MUTEX  1	/* ovewrite the pthread_[mutex|cond] functions */
#define CLH_MAX_THR    48
/* ******************************************************************************** */


static inline void* 
swap_ptr(volatile void* ptr, void *x) 
{
  asm volatile("xchgq %0,%1"
	       :"=r" ((unsigned long long) x)
	       :"m" (*(volatile long long *)ptr), "0" ((unsigned long long) x)
	       :"memory");

  return x;
}


#define CACHE_LINE_SIZE 64

#if !defined(PAUSE_IN)
#  define PAUSE_IN()			\
  ;
#endif

typedef struct clh_lock 
{
  volatile struct clh_lock_node* head;
#if PADDING == 1
  uint8_t padding[CACHE_LINE_SIZE - sizeof(struct clh_lock_node*)];
#endif
  volatile struct clh_lock_node* local[CLH_MAX_THR];
} clh_lock_t;

typedef struct clh_lock_node
{
  volatile uint64_t locked;
  volatile struct clh_lock_node* pred;
#if PADDING == 1
  uint8_t padding[CACHE_LINE_SIZE - sizeof(uint64_t) - sizeof(struct clh_lock*)];
#endif
} clh_lock_node_t;

#define CLH_LOCK_INITIALIZER { .head = NULL, .local = { 0 } }

typedef struct clh_cond
{
  uint32_t clh;
  volatile uint32_t head;
  clh_lock_t* l;
#if PADDING == 1
    uint8_t padding[CACHE_LINE_SIZE - 16];
#endif
} clh_cond_t;

#define CLH_COND_INITIALIZER { 0, 0, NULL }

inline int clh_lock_trylock(clh_lock_t* lock);
inline int clh_lock_lock(clh_lock_t* lock);
inline int clh_lock_unlock(clh_lock_t* lock);
inline int clh_lock_init(clh_lock_t* lock, pthread_mutexattr_t* a);
inline int clh_lock_destroy(clh_lock_t* the_lock);

#define USE_FUTEX_COND 1
#if USE_FUTEX_COND == 1

static inline int
sys_futex(void* addr1, int op, int val1, struct timespec* timeout, void* addr2, int val3)
{
  return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}

static inline int
clh_cond_wait(clh_cond_t* c, clh_lock_t* m)
{
  int head = c->head;

  if (c->l != m)
    {
      if (c->l) return EINVAL;
      
      /* Atomically set mutex inside cv */
      __attribute__ ((unused)) int dummy = (uintptr_t) __sync_val_compare_and_swap(&c->l, NULL, m);
      if (c->l != m) return EINVAL;
    }
  
  clh_lock_unlock(m);
  
  sys_futex((void*) &c->head, FUTEX_WAIT_PRIVATE, head, NULL, NULL, 0);
  
  clh_lock_lock(m);
  
  return 0;
}

static inline int
clh_cond_timedwait(clh_cond_t* c, clh_lock_t* m, const struct timespec* ts)
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
  
  clh_lock_unlock(m);
  
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
  clh_lock_lock(m);
  
  return ret;
}


static inline int
clh_cond_init(clh_cond_t* c, pthread_condattr_t* a)
{
  (void) a;
  
  c->l = NULL;
  
  /* Sequence variable doesn't actually matter, but keep valgrind happy */
  c->head = 0;
  
  return 0;
}

static inline int 
clh_cond_destroy(clh_cond_t* c)
{
  /* No need to do anything */
  (void) c;
  return 0;
}

static inline int
clh_cond_signal(clh_cond_t* c)
{
  /* We are waking someone up */
  __sync_add_and_fetch(&c->head, 1);
  
  /* Wake up a thread */
  sys_futex((void*) &c->head, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
  
  return 0;
}

static inline int
clh_cond_broadcast(clh_cond_t* c)
{
  clh_lock_t* m = c->l;
  
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
clh_cond_init(clh_cond_t* c, pthread_condattr_t* a)
{
  c->head = 0;
  c->clh = 0;
  c->l = NULL;
  return 0;
}

static inline int
clh_cond_destroy(clh_cond_t* c)
{
  c->head = -1;
  return 1;
}

static inline int
clh_cond_signal(clh_cond_t* c)
{
  if (c->clh > c->head)
    {
      __sync_fetch_and_add(&c->head, 1);
    }
  return 1;
}

static inline int 
clh_cond_broadcast(clh_cond_t* c)
{
  if (c->clh == c->head)
    {
      return 0;
    }

  uint32_t ch, ct;
  do
    {
      ch = c->head;
      ct = c->clh;
    }
  while (__sync_val_compare_and_swap(&c->head, ch, ct) != ch);

  return 1;
}

static inline int
clh_cond_wait(clh_cond_t* c, clh_lock_t* l)
{
  uint32_t cond_clh = ++c->clh;

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

  clh_lock_unlock(l);

  while (1)
    {
      int distance = cond_clh - c->head;
      if (distance == 0)
	{
	  break;
	}
      PAUSE_IN();
    }

  clh_lock_lock(l);
  return 1;
}

static inline int
clh_cond_timedwait(clh_cond_t* c, clh_lock_t* l, const struct timespec* ts)
{
  int ret = 0;

  uint32_t cond_clh = ++c->ticket;

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

  clh_lock_unlock(l);

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
  uint64_t to = clh_getticks() + ticks;

  while (cond_clh > c->head)
    {
      PAUSE_IN();
      if (clh_getticks() > to)
	{
	  ret = ETIMEDOUT;
	  break;
	}
    }

  clh_lock_lock(l);
  return ret;
}

#endif	/* USE_FUTEX_COND */

static inline int
clh_lock_timedlock(clh_lock_t* l, const struct timespec* ts)
{
  fprintf(stderr, "** warning -- pthread_mutex_timedlock not implemented\n");
  return 0;
}


#if REPLACE_MUTEX == 1
#  define pthread_mutex_init    clh_lock_init
#  define pthread_mutex_destroy clh_lock_destroy
#  define pthread_mutex_lock    clh_lock_lock
#  define pthread_mutex_timedlock clh_lock_timedlock
#  define pthread_mutex_unlock  clh_lock_unlock
#  define pthread_mutex_trylock clh_lock_trylock
#  define pthread_mutex_t       clh_lock_t
#  undef  PTHREAD_MUTEX_INITIALIZER
#  define PTHREAD_MUTEX_INITIALIZER CLH_LOCK_INITIALIZER

#  define pthread_cond_init     clh_cond_init
#  define pthread_cond_destroy  clh_cond_destroy
#  define pthread_cond_signal   clh_cond_signal
#  define pthread_cond_broadcast clh_cond_broadcast
#  define pthread_cond_wait     clh_cond_wait
#  define pthread_cond_timedwait clh_cond_timedwait
#  define pthread_cond_t        clh_cond_t
#  undef  PTHREAD_COND_INITIALIZER
#  define PTHREAD_COND_INITIALIZER CLH_COND_INITIALIZER
#endif

#ifdef __cplusplus
}
#endif

#endif


