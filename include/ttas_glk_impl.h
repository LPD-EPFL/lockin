/*
 * File: ttas_glk_impl.h
 * Author: Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description:
 *     TTAS implementation for GLS.
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


#ifndef _TTAS_IN_IMPL_H_
#define _TTAS_IN_IMPL_H_

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

#include "atomic_ops.h"
#include "gls_debug.h"

#if !defined(LOCK_IN_RLS_FENCE)
#  define LOCK_IN_RLS_FENCE()			\
  ;
  /* asm volatile ("mfence");	/\* mem fence when releasing the lock *\/ */
#endif				/* (used in tas_in, ttas_in */


#if !defined(likely)
#define likely(x)       __builtin_expect(!!(x), 1)
#endif

#if !defined(unlikely)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#endif




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

#if !defined(LOCK_IN_COOP)
#  define TTAS_COOP        0	/* spin for TTAS_MAX_SPINS before calling */
#else
#  define TTAS_COOP        LOCK_IN_COOP
#endif
#if !defined(LOCK_IN_MAX_SPINS)
#  define TTAS_MAX_SPINS   1024	/* sched_yield() to yield the cpu to others */
#else
#  define TTAS_MAX_SPINS   LOCK_IN_MAX_SPINS
#endif
#define FREQ_CPU_GHZ     2.8

#if defined(RTM)
#  if !defined(USE_HLE)
#    define USE_HLE        3	/* use hardware lock elision (hle)
				   0: don't use hle 
				   1: use plain hle
				   2: use rtm-based hle (pessimistic)
				   3: use rtm-based hle
				*/
#  endif
#  define HLE_RTM_OPT_RETRIES 3	/* num or retries if tx_start() fails */
#endif
/* ******************************************************************************** */

#if defined(RTM)
#  include <immintrin.h>
static inline uint32_t 
swap_uint32_hle_acq(volatile uint32_t* target,  uint32_t x) 
{
  asm volatile("XACQUIRE xchgl %0,%1"
	       :"=r" ((uint32_t) x)
	       :"m" (*(volatile uint32_t *)target), "0" ((uint32_t) x)
	       :"memory");

  return x;
}

static inline uint32_t 
swap_uint32_hle_rls(volatile uint32_t* target,  uint32_t x) 
{
  asm volatile("XRELEASE xchgl %0,%1"
	       :"=r" ((uint32_t) x)
	       :"m" (*(volatile uint32_t *)target), "0" ((uint32_t) x)
	       :"memory");

  return x;
}
#endif

#define TTAS_FREE   0
#define TTAS_LOCKED 1

#define CACHE_LINE_SIZE 64
#if !defined(PAUSE_IN)
#  define PAUSE_IN()			\
  ;
#endif

typedef struct ttas_lock 
{
  volatile uint32_t lock;
#if PADDING == 1
  uint8_t padding[CACHE_LINE_SIZE - sizeof(uint32_t)];
#endif
} ttas_lock_t;

#define TTAS_LOCK_INITIALIZER { .lock = TTAS_FREE }

typedef struct ttas_cond
{
  uint32_t ticket;
  volatile uint32_t head;
  ttas_lock_t* l;
#if PADDING == 1
    uint8_t padding[CACHE_LINE_SIZE - 16];
#endif
} ttas_cond_t;

#define TTAS_COND_INITIALIZER { 0, 0, NULL }


/* ******************************************************************************** */
#if USE_HLE == 0 || !defined(RTM) /* no hle */
/* ******************************************************************************** */

static inline int
ttas_lock_is_free(ttas_lock_t* lock)
{
  return lock->lock == TTAS_FREE;
}

/* returns 0 on success */
static inline int
ttas_lock_trylock(ttas_lock_t* lock) 
{
  if (lock->lock == TTAS_LOCKED)
    {
      return 1;
    }
  return swap_uint32(&lock->lock, TTAS_LOCKED);
}

static inline int
ttas_lock_lock(ttas_lock_t* lock) 
{
  do
    {
      if (likely(swap_uint32(&lock->lock, TTAS_LOCKED) == TTAS_FREE))
	{
	  break;
	}

#  if TTAS_COOP == 1
      size_t spins = 0;
      while (lock->lock != TTAS_FREE)
	{
	  PAUSE_IN();
	  if (spins++ == TTAS_MAX_SPINS)
	    {
	      GLS_DD_CHECK(spins);
	      sched_yield();
	      spins = 0;
	    }
	}
#  else
      GLS_DDD(size_t n_spins = 0);
      while (lock->lock != TTAS_FREE)
	{
	  GLS_DD_CHECK(n_spins);
	  PAUSE_IN();
	}
#  endif
    }
  while (1);
  return 0;
}

static inline int
ttas_lock_unlock(ttas_lock_t* lock) 
{
  asm volatile("" ::: "memory");
  lock->lock = TTAS_FREE;
  asm volatile("" ::: "memory");
  LOCK_IN_RLS_FENCE();
  return 0;
}

/* ******************************************************************************** */
#elif USE_HLE == 1		/* plain hle */
/* ******************************************************************************** */

/* returns 0 on success */
static inline int
ttas_lock_trylock(ttas_lock_t* lock) 
{
  if (lock->lock == TTAS_LOCKED)
    {
      return 1;
    }
  return (swap_uint32_hle_acq(&lock->lock, TTAS_LOCKED));
}

static inline int
ttas_lock_lock(ttas_lock_t* lock) 
{
  do
    {
      while (lock->lock != TTAS_FREE)
	{
	  PAUSE_IN();
	}
    }
  while (swap_uint32_hle_acq(&lock->lock, TTAS_LOCKED));
  return 0;
}

static inline int
ttas_lock_unlock(ttas_lock_t* lock) 
{
  swap_uint32_hle_rls(&lock->lock, TTAS_FREE);
  return 0;
}

/* ******************************************************************************** */
#elif USE_HLE == 2		/* rtm-based hle (pessimistic) */
/* ******************************************************************************** */

/* returns 0 on success */
static inline int
ttas_lock_trylock(ttas_lock_t* lock) 
{
  if (lock->lock == TTAS_LOCKED)
    {
      return 1;
    }

  if (_xbegin() == _XBEGIN_STARTED)
    {
      if (lock->lock == TTAS_FREE)
	{
	  return 0;
	}

      _xend();
      return 1;
    }

  return __atomic_exchange_n(&lock->lock, TTAS_LOCKED, __ATOMIC_ACQUIRE);
}

static inline int
ttas_lock_lock(ttas_lock_t* lock) 
{
  while (lock->lock == TTAS_LOCKED)
    {
      PAUSE_IN();
    }

  if (_xbegin() == _XBEGIN_STARTED)
    {
      if (lock->lock == TTAS_FREE)
	{
	  return 0;
	}

      _xabort(0xff);
    }
  else
    {
      do
	{
	  while (lock->lock == TTAS_LOCKED)
	    {
	      PAUSE_IN();
	    }
	}
      while (__atomic_exchange_n(&lock->lock, TTAS_LOCKED, __ATOMIC_ACQUIRE));
    }

  return 0;
}

static inline int
ttas_lock_unlock(ttas_lock_t* lock) 
{
  if (lock->lock == TTAS_FREE && _xtest())
    {
      _xend();
    }
  else
    {
      __atomic_clear(&lock->lock, __ATOMIC_RELEASE);
    }
  return 0;
}

/* ******************************************************************************** */
#elif USE_HLE == 3		/* rtm-based hle (optimistic) */
/* ******************************************************************************** */

static inline void
ttas_pause_rep(int n)
{
  while (n--)
    {
      PAUSE_IN();
    }
}

/* returns 0 on success */
static inline int
ttas_lock_trylock(ttas_lock_t* lock) 
{
  if (lock->lock == TTAS_LOCKED)
    {
      return 1;
    }

  int t;
  for (t = 0; t < HLE_RTM_OPT_RETRIES; t++)
    {
      if (_xbegin() == _XBEGIN_STARTED)
	{
	  if (lock->lock == 0)
	    {
	      return 0;
	    }

	  _xend();
	  return 1;
	}
      ttas_pause_rep(2 << t);
    }

  return __atomic_exchange_n(&lock->lock, TTAS_LOCKED, __ATOMIC_ACQUIRE);
}

static inline int
ttas_lock_lock(ttas_lock_t* lock) 
{
  while (lock->lock == TTAS_LOCKED)
    {
      PAUSE_IN();
    }

  int t;
  for (t = 0; t < HLE_RTM_OPT_RETRIES; t++)
    {
      long status;
      if ((status = _xbegin()) == _XBEGIN_STARTED)
	{
	  if (lock->lock == 0)
	    {
	      return 0;
	    }

	  _xabort(0xff);
	}
      else
	{
	  if (status & _XABORT_EXPLICIT)
	    {
	      break;
	    }
	  ttas_pause_rep(2 << t);
	}
    }


  do
    {
      while (lock->lock == TTAS_LOCKED)
	{
	  PAUSE_IN();
	}
    }
  while (__atomic_exchange_n(&lock->lock, 1, __ATOMIC_ACQUIRE));
  return 0;
}

static inline int
ttas_lock_unlock(ttas_lock_t* lock) 
{
  if (lock->lock == TTAS_FREE && _xtest())
    {
      _xend();
    }
  else
    {
      __atomic_clear(&lock->lock, __ATOMIC_RELEASE);
    }
  return 0;
}

/* ******************************************************************************** */
#endif	/* USE_HLE */
/* ******************************************************************************** */


static inline int
ttas_lock_init(ttas_lock_t* the_lock, const pthread_mutexattr_t* a) 
{
  the_lock->lock = TTAS_FREE;
  asm volatile ("mfence");
  return 0;
}

static inline int
ttas_lock_destroy(ttas_lock_t* the_lock) 
{
  return 0;
}

#if USE_FUTEX_COND == 1

static inline int
sys_futex_ttas(void* addr1, int op, int val1, struct timespec* timeout, void* addr2, int val3)
{
  return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}

static inline int
ttas_cond_wait(ttas_cond_t* c, ttas_lock_t* m)
{
  int head = c->head;

  if (c->l != m)
    {
      if (c->l) return EINVAL;
      
      /* Atomically set mutex inside cv */
      __attribute__ ((unused)) int dummy = (uintptr_t) __sync_val_compare_and_swap(&c->l, NULL, m);
      if (c->l != m) return EINVAL;
    }
  
  ttas_lock_unlock(m);
  
  sys_futex_ttas((void*) &c->head, FUTEX_WAIT_PRIVATE, head, NULL, NULL, 0);
  
  ttas_lock_lock(m);
  
  return 0;
}

static inline int
ttas_cond_timedwait(ttas_cond_t* c, ttas_lock_t* m, const struct timespec* ts)
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
  
  ttas_lock_unlock(m);
  
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

  sys_futex_ttas((void*) &c->head, FUTEX_WAIT_PRIVATE, head, &rt, NULL, 0);
  
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
  ttas_lock_lock(m);
  
  return ret;
}

static inline int
ttas_cond_init(ttas_cond_t* c, const pthread_condattr_t* a)
{
  (void) a;
  
  c->l = NULL;
  
  /* Sequence variable doesn't actually matter, but keep valgrind happy */
  c->head = 0;
  
  return 0;
}

static inline int 
ttas_cond_destroy(ttas_cond_t* c)
{
  /* No need to do anything */
  (void) c;
  return 0;
}

static inline int
ttas_cond_signal(ttas_cond_t* c)
{
  /* We are waking someone up */
  __sync_add_and_fetch(&c->head, 1);
  
  /* Wake up a thread */
  sys_futex_ttas((void*) &c->head, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
  
  return 0;
}

static inline int
ttas_cond_broadcast(ttas_cond_t* c)
{
  ttas_lock_t* m = c->l;
  
  /* No mutex means that there are no waiters */
  if (!m) return 0;
  
  /* We are waking everyone up */
  __sync_add_and_fetch(&c->head, 1);
  
  /* Wake one thread, and requeue the rest on the mutex */
  sys_futex_ttas((void*) &c->head, FUTEX_REQUEUE_PRIVATE, INT_MAX, NULL, NULL, 0);
  
  return 0;
}

#else

static inline int
ttas_cond_init(ttas_cond_t* c, pthread_condattr_t* a)
{
  c->head = 0;
  c->ticket = 0;
  c->l = NULL;
  return 0;
}

static inline int
ttas_cond_destroy(ttas_cond_t* c)
{
  c->head = -1;
  return 1;
}

static inline int
ttas_cond_signal(ttas_cond_t* c)
{
  if (c->ticket > c->head)
    {
      __sync_fetch_and_add(&c->head, 1);
    }
  return 1;
}

static inline int 
ttas_cond_broadcast(ttas_cond_t* c)
{
  if (c->ticket == c->head)
    {
      return 0;
    }

  uint32_t ch, ct;
  do
    {
      ch = c->head;
      ct = c->ticket;
    }
  while (__sync_val_compare_and_swap(&c->head, ch, ct) != ch);

  return 1;
}

static inline int
ttas_cond_wait(ttas_cond_t* c, ttas_lock_t* l)
{
  uint32_t cond_ttas = ++c->ticket;

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

  ttas_lock_unlock(l);

  while (cond_ttas > c->head)
    {
      PAUSE_IN();
    }

  ttas_lock_lock(l);
  return 1;
}

#  if defined(__x86_64__)
static inline uint64_t
ttas_getticks(void)
{
  unsigned hi, lo;
  __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
  return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}
#  endif

static inline int
ttas_cond_timedwait(ttas_cond_t* c, ttas_lock_t* l, const struct timespec* ts)
{
  int ret = 0;

  uint32_t cond_ttas = ++c->ticket;

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

  ttas_lock_unlock(l);

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
  uint64_t to = ttas_getticks() + ticks;

  while (cond_ttas > c->head)
    {
      PAUSE_IN();
      if (ttas_getticks() > to)
	{
	  ret = ETIMEDOUT;
	  break;
	}
    }

  ttas_lock_lock(l);
  return ret;
}

#endif

static inline int
ttas_lock_timedlock(ttas_lock_t* l, const struct timespec* ts)
{
  fprintf(stderr, "** warning -- pthread_mutex_timedlock not implemented\n");
  return 0;
}

#ifdef __cplusplus
}
#endif

#endif


