/*
 * File: mutex_glk_impl.h
 * Author: Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description:
 *     MUTEX implementation for GLS.
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

/* 
 * The mutex is an adaptive spin-futex lock.
 * Mutex measures the spin-to-mutex acquire (or release) ratio and adjusts
 * the spinning behavior of the lock accordingly. For example, when the ratio
 * is below the target limit, mutex might try to increase the number of 
 * spins. If this increment is unsucessful, it might decides to increase it 
 * further, or to simply become a futex-lock. Mutex eventuall reaches some
 * stable states, but never stops trying to find a better stable state.
 */


#ifndef _MUTEX_IN_IMP_H_
#define _MUTEX_IN_IMP_H_

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

#include <gls_debug.h>

#if !defined(__x86_64__)
#  error This file is designed to work only on x86_64 architectures! 
#endif

  /* ******************************************************************************** */
  /* settings *********************************************************************** */
#ifndef PADDING
#  define PADDING        1        /* padd locks/conditionals to cache-line */
#endif
#define FREQ_CPU_GHZ   2.8	/* core frequency in GHz */
#define MUTEX_SPIN_TRIES_LOCK       8192 /* spinning retries before futex */
#define MUTEX_SPIN_TRIES_LOCK_MIN   256 /* spinning retries before futex */
#define MUTEX_SPIN_TRIES_UNLOCK     384  /* spinning retries before futex wake */
#define MUTEX_SPIN_TRIES_UNLOCK_MIN 128  /* spinning retries before futex wake */

#define MUTEX_SPIN_MAX            1024

#define MUTEX_DO_ADAP             0
#define MUTEX_ADAP_EVERY          2048
#define MUTEX_RETRY_SPIN_EVERY     8
#define MUTEX_RETRY_SPIN_EVERY_MAX 32
#define MUTEX_FUTEX_LIM           128
#define MUTEX_FUTEX_LIM_MAX       256
#define MUTEX_PRINT               0 /* print debug output  */
#define MUTEX_FAIR                0 /* 0: don't care
					 1: with wake-ups */

#if MUTEX_DO_ADAP == 1
#  define MUTEX_ADAP(d)	    d
#else
#  define MUTEX_ADAP(d)
#endif
  /* ******************************************************************************** */

#if defined(PAUSE_IN)
#  undef PAUSE_IN
#  define PAUSE_IN()				\
  asm volatile ("mfence");
#endif

  static inline void
  mutex_cdelay(const int cycles)
  {
    int cy = cycles;
    while (cy--)
      {
	asm volatile ("");
      }
  }


  //Swap uint32_t
  static inline uint32_t
  mutex_swap_uint32(volatile uint32_t* target,  uint32_t x)
  {
    asm volatile("xchgl %0,%1"
		 :"=r" ((uint32_t) x)
		 :"m" (*(volatile uint32_t *)target), "0" ((uint32_t) x)
		 :"memory");

    return x;
  }

  //Swap uint8_t
  static inline uint8_t
  mutex_swap_uint8(volatile uint8_t* target,  uint8_t x)
  {
    asm volatile("xchgb %0,%1"
		 :"=r" ((uint8_t) x)
		 :"m" (*(volatile uint8_t *)target), "0" ((uint8_t) x)
		 :"memory");

    return x;
  }
#define cmpxchg(a, b, c) __sync_val_compare_and_swap(a, b, c)
#define xchg_32(a, b)    mutex_swap_uint32((uint32_t*) a, b)
#define xchg_8(a, b)     mutex_swap_uint8((uint8_t*) a, b)
#define atomic_add(a, b) __sync_fetch_and_add(a, b)

#define CACHE_LINE_SIZE 64

  typedef struct mutex_lock
  {
    union
    {
      volatile unsigned u;
      struct
      {
	volatile unsigned char locked;
	volatile unsigned char contended;
      } b;
    } l;
#if MUTEX_DO_ADAP == 1
    uint8_t padding[4];

    unsigned int n_spins;
    unsigned int n_spins_unlock;
    size_t n_acq;
    unsigned int n_miss;
    unsigned int n_miss_limit;
    unsigned int is_futex;
    unsigned int n_acq_first_sleep;
    unsigned int retry_spin_every;
    unsigned int padding3;
    uint8_t padding2[CACHE_LINE_SIZE - 6 * sizeof(size_t)];
#else
#  if PADDING == 1
    uint8_t padding_cl[CACHE_LINE_SIZE - sizeof(unsigned)];
#  endif
#endif
  } mutex_lock_t;

#define STATIC_ASSERT(a, msg)           _Static_assert ((a), msg);

  /* STATIC_ASSERT((sizeof(mutex_lock_t) == 64) || (sizeof(mutex_lock_t) == 4),  */
  /* 		"sizeof(mutex_lock_t)"); */


#if MUTEX_DO_ADAP == 1
#define MUTEX_INITIALIZER				\
  {							\
    .l.u = 0,						\
      .n_spins = MUTEX_SPIN_TRIES_LOCK,		\
      .n_spins_unlock = MUTEX_SPIN_TRIES_UNLOCK,	\
      .n_acq = 0,	              			\
      .n_miss = 0,					\
      .n_miss_limit = MUTEX_FUTEX_LIM,		\
      .is_futex = 0,					\
      .n_acq_first_sleep = 0,				\
      .retry_spin_every = MUTEX_RETRY_SPIN_EVERY,	\
      }
#else
#define MUTEX_INITIALIZER			\
  {						\
    .l.u = 0,					\
      }
#endif
  typedef struct upmutex_cond1
  {
    mutex_lock_t* m;
    int seq;
    int pad;
#if PADDING == 1
    uint8_t padding[CACHE_LINE_SIZE - 16];
#endif
  } upmutex_cond1_t;

#define UPMUTEX_COND1_INITIALIZER {NULL, 0, 0}

  static inline int
  sys_futex_mutex(void* addr1, int op, int val1, struct timespec* timeout, void* addr2, int val3)
  {
    return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
  }

  static inline int
  mutex_init(mutex_lock_t* m, const pthread_mutexattr_t* a)
  {
    (void) a;
    m->l.u = 0;
#if MUTEX_DO_ADAP == 1
    m->n_spins = MUTEX_SPIN_TRIES_LOCK;
    m->n_spins_unlock = MUTEX_SPIN_TRIES_UNLOCK;
    m->n_acq = 0;
    m->n_miss = 0;
    m->n_miss_limit = MUTEX_FUTEX_LIM;
    m->is_futex = 0;
    m->n_acq_first_sleep = 0;
    m->retry_spin_every = MUTEX_RETRY_SPIN_EVERY;
#endif
    return 0;
  }

  static inline int
  mutex_destroy(mutex_lock_t* m)
  {
    /* Do nothing */
    (void) m;
    return 0;
  }

#define __mutex_unlikely(x) __builtin_expect((x), 0)

  static inline uint64_t mutex_getticks(void)
  {
    unsigned hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
  }

#define MUTEX_FOR_N_CYCLES(n, do)		\
  {						\
  uint64_t ___s = mutex_getticks();		\
  while (1)					\
    {						\
      do;					\
      uint64_t ___e = mutex_getticks();	\
      if ((___e - ___s) > n)			\
	{					\
	  break;				\
	}					\
    }						\
  }

  static inline int
  mutex_lock(mutex_lock_t* m)
  {
    if (!xchg_8(&m->l.b.locked, 1))
      {
	return 0;
      }

#if MUTEX_DO_ADAP == 1
    const register unsigned int time_spin = m->n_spins;
#else
    const unsigned int time_spin = MUTEX_SPIN_TRIES_LOCK;
#endif
    MUTEX_FOR_N_CYCLES(time_spin,
			 if (!xchg_8(&m->l.b.locked, 1))
			   {
			     return 0;
			   }
			 PAUSE_IN();
			 );


    /* Have to sleep */
    GLS_DDD(size_t n_spins = 0);
    while (xchg_32(&m->l.u, 257) & 1)
      {
#if MUTEX_FAIR == 1
	__sync_val_compare_and_swap(&m->n_acq_first_sleep, 0, m->n_acq);
#endif
#if GLS_DEBUG_MODE == GLS_DEBUG_DEADLOCK
	struct timespec rt = { .tv_sec = 0, .tv_nsec = 250000000L };
	sys_futex_mutex(m, FUTEX_WAIT_PRIVATE, 257, &rt, NULL, 0);
	GLS_DD_CHECK_FAST(n_spins);
#else
	sys_futex_mutex(m, FUTEX_WAIT_PRIVATE, 257, NULL, NULL, 0);
#endif
      }
#if MUTEX_FAIR == 1
    m->n_acq_first_sleep = 0;
#endif
    return 0;
  }

  static inline void
  mutex_lock_training(mutex_lock_t* m)
  {
#if MUTEX_DO_ADAP == 1
    if (__mutex_unlikely((++m->n_acq & MUTEX_ADAP_EVERY) == 0))
      {
#if MUTEX_FAIR == 1
	int n_acq_first_sleep = m->n_acq_first_sleep;
	int n_acq_diff = m->n_acq - n_acq_first_sleep;
	if (n_acq_first_sleep && n_acq_diff > MUTEX_SPIN_MAX)
	  {
	    /* m->n_acq_first_sleep = 0; */
	    sys_futex_mutex(m, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
	  }
#endif
	if (!m->is_futex)
	  {
	    if (m->n_miss > m->n_miss_limit)
	      {
#if MUTEX_PRINT == 1
		printf("[MUTEX] n_miss = %d  > %d :: switch to mutex\n", m->n_miss, m->n_miss_limit);
#endif
		m->n_spins = MUTEX_SPIN_TRIES_LOCK_MIN;
		m->n_spins_unlock = MUTEX_SPIN_TRIES_UNLOCK_MIN;
		m->is_futex = 1;
	      }
	  }
	else
	  {
	    unsigned int re = m->retry_spin_every;
	    if (m->is_futex++ == re)
	      {
		if (re < MUTEX_RETRY_SPIN_EVERY_MAX)
		  {
		    re <<= 1;
		  }
		m->retry_spin_every = re;
		/* m->n_miss_limit++; */
		if (m->n_miss_limit < MUTEX_FUTEX_LIM_MAX)
		  {
		    m->n_miss_limit++;
		  }
		m->is_futex = 0;
#if MUTEX_PRINT == 1
		printf("[MUTEX] TRY :: switch to spinlock\n");
#endif
		m->n_spins = MUTEX_SPIN_TRIES_LOCK;
		m->n_spins_unlock = MUTEX_SPIN_TRIES_UNLOCK;
	      }
	  }
	m->n_miss = 0;
      }
#endif
  }


  static inline int
  mutex_unlock(mutex_lock_t* m)
  {
    /* Locked and not contended */
    if ((m->l.u == 1) && (cmpxchg(&m->l.u, 1, 0) == 1))
      {
	return 0;
      }

    MUTEX_ADAP(mutex_lock_training(m););

    /* Unlock */
    m->l.b.locked = 0;
    asm volatile ("mfence");
    if (m->l.b.locked)
      {
	return 0;
      }

    /* We need to wake someone up */
    m->l.b.contended = 0;

    MUTEX_ADAP(m->n_miss++;);
    sys_futex_mutex(m, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
    return 0;
  }

  static inline int
  mutex_lock_is_free(mutex_lock_t* lock)
  {
    return lock->l.b.locked == 0;
  }

  static inline int
  mutex_lock_trylock(mutex_lock_t* m)
  {
    unsigned c = xchg_8(&m->l.b.locked, 1);
    if (!c) return 0;
    return EBUSY;
  }


  /* ******************************************************************************** */
  /* condition variables */
  /* ******************************************************************************** */

  static inline int
  upmutex_cond1_init(upmutex_cond1_t* c, const pthread_condattr_t* a)
  {
    (void) a;

    c->m = NULL;

    /* Sequence variable doesn't actually matter, but keep valgrind happy */
    c->seq = 0;

    return 0;
  }

  static inline int
  upmutex_cond1_destroy(upmutex_cond1_t* c)
  {
    /* No need to do anything */
    (void) c;
    return 0;
  }

  static inline int
  upmutex_cond1_signal(upmutex_cond1_t* c)
  {
    /* We are waking someone up */
    atomic_add(&c->seq, 1);

    /* Wake up a thread */
    sys_futex_mutex(&c->seq, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);

    return 0;
  }

  static inline int
  upmutex_cond1_broadcast(upmutex_cond1_t* c)
  {
    mutex_lock_t* m = c->m;

    /* No mutex means that there are no waiters */
    if (!m) return 0;

    /* We are waking everyone up */
    atomic_add(&c->seq, 1);

    /* Wake one thread, and requeue the rest on the mutex */
    sys_futex_mutex(&c->seq, FUTEX_REQUEUE_PRIVATE, 1, (struct timespec *) INT_MAX, m, 0);

    return 0;
  }

  static inline int
  upmutex_cond1_wait(upmutex_cond1_t* c, mutex_lock_t* m)
  {
    int seq = c->seq;

    if (c->m != m)
      {
	if (c->m) return EINVAL;
	/* Atomically set mutex inside cv */
	__attribute__ ((unused)) int dummy = (uintptr_t) cmpxchg(&c->m, NULL, m);
	if (c->m != m) return EINVAL;
      }

    mutex_unlock(m);

    sys_futex_mutex(&c->seq, FUTEX_WAIT_PRIVATE, seq, NULL, NULL, 0);

    while (xchg_32(&m->l.b.locked, 257) & 1)
      {
	sys_futex_mutex(m, FUTEX_WAIT_PRIVATE, 257, NULL, NULL, 0);
      }

    return 0;
  }


  static inline int
  mutex_cond_timedwait(upmutex_cond1_t* c, mutex_lock_t* m, const struct timespec* ts)
  {
    int ret = 0;
    int seq = c->seq;

    if (c->m != m)
      {
	if (c->m) return EINVAL;
	/* Atomically set mutex inside cv */
	__attribute__ ((unused)) int dummy = (uintptr_t) cmpxchg(&c->m, NULL, m);
	if (c->m != m) return EINVAL;
      }

    mutex_unlock(m);

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

    sys_futex_mutex(&c->seq, FUTEX_WAIT_PRIVATE, seq, &rt, NULL, 0);

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
    while (xchg_32(&m->l.b.locked, 257) & 1)
      {
	sys_futex_mutex(m, FUTEX_WAIT_PRIVATE, 257, NULL, NULL, 0);
      }

    return ret;
  }

  static inline int
  mutex_lock_timedlock(mutex_lock_t* l, const struct timespec* ts)
  {
    fprintf(stderr, "** warning -- pthread_mutex_timedlock not implemented\n");
    return 0;
  }


#ifdef __cplusplus
}
#endif

#endif

