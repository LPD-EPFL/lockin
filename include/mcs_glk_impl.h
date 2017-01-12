/*
 * File: mcs_glk_impl.h
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


#ifndef _MCS_IN_IMPL_H_
#define _MCS_IN_IMPL_H_

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

/* ******************************************************************************** */
/* settings *********************************************************************** */
#define USE_FUTEX_COND 1	/* use futex-based (1) or spin-based futexs */
#ifndef PADDING
#  define PADDING        1        /* padd locks/conditionals to cache-line */
#endif
#define FREQ_CPU_GHZ   2.8	/* core frequency in GHz */
#define MCS_MAX_THR    81
#define MCS_NESTED_LOCKS_MAX 16
#if !defined(LOCK_IN_COOP)
#  define MCS_COOP        0	/* spin for MCS_MAX_SPINS before calling */
#else
#  define MCS_COOP        LOCK_IN_COOP
#endif
#if !defined(LOCK_IN_MAX_SPINS)
#  define MCS_MAX_SPINS   1024	/* sched_yield() to yield the cpu to others */
#else
#  define MCS_MAX_SPINS   LOCK_IN_MAX_SPINS
#endif
/* ******************************************************************************** */
 

static inline void* 
swap_ptr_mcs(volatile void* ptr, void *x)
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

typedef volatile struct mcs_lock 
{
  volatile uint64_t waiting;
  volatile struct mcs_lock* next;
#if PADDING == 1
  uint8_t padding[CACHE_LINE_SIZE - sizeof(uint64_t) - sizeof(struct mcs_lock*)];
#endif
} mcs_lock_t;

typedef volatile struct mcs_lock_local_s
{
  mcs_lock_t* head[MCS_NESTED_LOCKS_MAX];
#if PADDING == 1
  uint8_t head_padding[2*CACHE_LINE_SIZE - MCS_NESTED_LOCKS_MAX*sizeof(mcs_lock_t*)];
#endif
  mcs_lock_t lock[MCS_NESTED_LOCKS_MAX];
#if PADDING == 1
  //uint8_t lock_padding[CACHE_LINE_SIZE - MCS_NESTED_LOCKS_MAX*sizeof(mcs_lock_t)];
#endif
} mcs_lock_local_t;

#define MCS_LOCK_INITIALIZER { .waiting = 0, .next = NULL }

extern __thread mcs_lock_local_t __mcs_local;
#define MCS_LOCAL_DATA __thread mcs_lock_local_t __mcs_local = { .head = { NULL, NULL, NULL, NULL} }

typedef struct mcs_cond
{
  uint32_t mcs;
  volatile uint32_t head;
  mcs_lock_t* l;
#if PADDING == 1
    uint8_t padding[CACHE_LINE_SIZE - 16];
#endif
} mcs_cond_t;
#define MCS_COND_INITIALIZER { 0, 0, NULL }

extern int mcs_lock_trylock(mcs_lock_t* lock);
extern int mcs_lock_lock(mcs_lock_t* lock);
extern int mcs_lock_queue_length(mcs_lock_t* lock);
extern int mcs_lock_unlock(mcs_lock_t* lock);
extern int mcs_lock_init(mcs_lock_t* lock, pthread_mutexattr_t* a);
extern int mcs_lock_destroy(mcs_lock_t* the_lock);
extern mcs_lock_t* mcs_get_local_lock(mcs_lock_t* head);
extern mcs_lock_t* mcs_get_local_unlock(mcs_lock_t* head);
extern mcs_lock_t* mcs_get_local_nochange(mcs_lock_t* head);

static inline int
mcs_lock_is_free(mcs_lock_t* lock)
{
  return lock->next == NULL;
}

#define USE_FUTEX_COND 1
#if USE_FUTEX_COND == 1

static inline int
sys_futex(void* addr1, int op, int val1, struct timespec* timeout, void* addr2, int val3)
{
  return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}

static inline int
mcs_cond_wait(mcs_cond_t* c, mcs_lock_t* m)
{
  int head = c->head;

  if (c->l != m)
    {
      if (c->l) return EINVAL;
      
      /* Atomically set mutex inside cv */
      __attribute__ ((unused)) int dummy = (uintptr_t) __sync_val_compare_and_swap(&c->l, NULL, m);
      if (c->l != m) return EINVAL;
    }
  
  mcs_lock_unlock(m);
  
  sys_futex((void*) &c->head, FUTEX_WAIT_PRIVATE, head, NULL, NULL, 0);
  
  mcs_lock_lock(m);
  
  return 0;
}

static inline int
mcs_cond_timedwait(mcs_cond_t* c, mcs_lock_t* m, const struct timespec* ts)
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
  
  mcs_lock_unlock(m);
  
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
  mcs_lock_lock(m);
  
  return ret;
}

static inline int
mcs_cond_init(mcs_cond_t* c, pthread_condattr_t* a)
{
  (void) a;
  
  c->l = NULL;
  
  /* Sequence variable doesn't actually matter, but keep valgrind happy */
  c->head = 0;
  
  return 0;
}

static inline int 
mcs_cond_destroy(mcs_cond_t* c)
{
  /* No need to do anything */
  (void) c;
  return 0;
}

static inline int
mcs_cond_signal(mcs_cond_t* c)
{
  /* We are waking someone up */
  __sync_add_and_fetch(&c->head, 1);
  
  /* Wake up a thread */
  sys_futex((void*) &c->head, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
  
  return 0;
}

static inline int
mcs_cond_broadcast(mcs_cond_t* c)
{
  mcs_lock_t* m = c->l;
  
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
mcs_cond_init(mcs_cond_t* c, pthread_condattr_t* a)
{
  c->head = 0;
  c->mcs = 0;
  c->num_total = 0;
  c->num_waited = 0;
  c->l = NULL;
  return 0;
}

static inline int
mcs_cond_destroy(mcs_cond_t* c)
{
  c->head = -1;
  return 1;
}

static inline int
mcs_cond_signal(mcs_cond_t* c)
{
  if (c->mcs > c->head)
    {
      __sync_fetch_and_add(&c->head, 1);
    }
  return 1;
}

static inline int 
mcs_cond_broadcast(mcs_cond_t* c)
{
  if (c->mcs == c->head)
    {
      return 0;
    }

  uint32_t ch, ct;
  do
    {
      ch = c->head;
      ct = c->mcs;
    }
  while (__sync_val_compare_and_swap(&c->head, ch, ct) != ch);

  return 1;
}

static inline int
mcs_cond_wait(mcs_cond_t* c, mcs_lock_t* l)
{
  uint32_t cond_mcs = ++c->mcs;

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

  mcs_lock_unlock(l);

  while (1)
    {
      int distance = cond_mcs - c->head;
      if (distance == 0)
	{
	  break;
	}
      PAUSE_IN();
    }

  mcs_lock_lock(l);
  return 1;
}

static inline int
mcs_cond_timedwait(mcs_cond_t* c, mcs_lock_t* l, const struct timespec* ts)
{
  int ret = 0;

  uint32_t cond_mcs = ++c->ticket;

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

  mcs_lock_unlock(l);

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
  uint64_t to = mcs_getticks() + ticks;

  while (cond_mcs > c->head)
    {
      PAUSE_IN();
      if (mcs_getticks() > to)
	{
	  ret = ETIMEDOUT;
	  break;
	}
    }

  mcs_lock_lock(l);
  return ret;
}

#endif	/* USE_FUTEX_COND */

static inline int
mcs_lock_timedlock(mcs_lock_t* l, const struct timespec* ts)
{
  fprintf(stderr, "** warning -- pthread_mutex_timedlock not implemented\n");
  return 0;
}

#ifdef __cplusplus
}
#endif

#endif


