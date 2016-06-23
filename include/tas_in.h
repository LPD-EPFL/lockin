/*
 * File: tas_in.h
 * Author: Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description: 
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


#ifndef _TAS_IN_H_
#define _TAS_IN_H_

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

#define LOCK_IN_NAME "TAS"

/* ******************************************************************************** */
/* settings *********************************************************************** */
#if !defined(USE_FUTEX_COND)
#  define USE_FUTEX_COND 1	/* use futex-based (1) or spin-based futexs */
#endif
#if !defined(PADDING)
#  define PADDING        1        /* padd locks/conditionals to cache-line */
#endif
#if !defined(LOCK_IN_COOP)
#  define TAS_COOP       1	/* spin for TAS_MAX_SPINS before calling */
#else
#  define TAS_COOP       LOCK_IN_COOP
#endif
#if !defined(LOCK_IN_MAX_SPINS)
#  define TAS_MAX_SPINS    256	/* sched_yield() to yield the cpu to others */
#else
#  define TAS_MAX_SPINS LOCK_IN_MAX_SPINS
#endif
#define FREQ_CPU_GHZ     2.8
#define REPLACE_MUTEX    1	/* ovewrite the pthread_[mutex|cond] functions */

#if defined(RTM)
#  if !defined(USE_HLE)
#    define USE_HLE      2	/* use hardware lock elision (hle)
				   0: don't use hle 
				   1: use plain hle
				   2: use rtm-based hle (pessimistic)
				   3: use rtm-based hle
				*/
#  endif
#  define HLE_RTM_OPT_RETRIES 3	/* num or retries if tx_start() fails */
#endif				/* RTM */
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


#define TAS_FREE   0
#define TAS_LOCKED 1

#define CACHE_LINE_SIZE 64
#if !defined(PAUSE_IN)
#  define PAUSE_IN()			\
  ;
#endif

static inline uint32_t 
swap_uint32(volatile uint32_t* target,  uint32_t x) 
{
  asm volatile ("xchgl %0,%1"
		:"=r" ((uint32_t) x)
		:"m" (*(volatile uint32_t *)target), "0" ((uint32_t) x)
		:"memory");

  return x;
}

typedef struct tas_lock 
{
  volatile uint32_t lock;
#if PADDING == 1
  uint8_t padding[CACHE_LINE_SIZE - sizeof(uint32_t)];
#endif
} tas_lock_t;

#define TAS_LOCK_INITIALIZER { .lock = TAS_FREE }

typedef struct tas_cond
{
  uint32_t ticket;
  volatile uint32_t head;
  tas_lock_t* l;
#if PADDING == 1
    uint8_t padding[CACHE_LINE_SIZE - 16];
#endif
} tas_cond_t;

#define TAS_COND_INITIALIZER { 0, 0, NULL }


/* ******************************************************************************** */
#if USE_HLE == 0 || !defined(RTM) /* no hle */
/* ******************************************************************************** */

/* returns 0 on success */
static inline int
tas_lock_trylock(tas_lock_t* lock) 
{
  return (swap_uint32(&lock->lock, TAS_LOCKED));
}

static inline int
tas_lock_lock(tas_lock_t* lock) 
{
#if TAS_COOP == 1
  size_t spins = 0;
  while (swap_uint32(&lock->lock, TAS_LOCKED))
    {
      PAUSE_IN();
      if (spins++ == TAS_MAX_SPINS)
	{
	  sched_yield();
	  spins = 0;
	}
    }
#else
  while (swap_uint32(&lock->lock, TAS_LOCKED))
    {
      PAUSE_IN();
    }
#endif
  return 0;
}

static inline int
tas_lock_unlock(tas_lock_t* lock) 
{
  asm volatile("" ::: "memory");
  lock->lock = TAS_FREE;
  LOCK_IN_RLS_FENCE();
  return 0;
}

/* ******************************************************************************** */
#elif USE_HLE == 1		/* plain hle */
/* ******************************************************************************** */

/* returns 0 on success */
static inline int
tas_lock_trylock(tas_lock_t* lock) 
{
  return (swap_uint32_hle_acq(&lock->lock, TAS_LOCKED));
}

static inline int
tas_lock_lock(tas_lock_t* lock) 
{
  while (swap_uint32_hle_acq(&lock->lock, TAS_LOCKED))
    {
      PAUSE_IN();
    }
  return 0;
}

static inline int
tas_lock_unlock(tas_lock_t* lock) 
{
  swap_uint32_hle_rls(&lock->lock, TAS_FREE);
  return 0;
}

/* ******************************************************************************** */
#elif USE_HLE == 2		/* rtm-based hle (pessimistic) */
/* ******************************************************************************** */

/* returns 0 on success */
static inline int
tas_lock_trylock(tas_lock_t* lock) 
{
  if (_xbegin() == _XBEGIN_STARTED)
    {
      if (lock->lock == TAS_FREE)
	{
	  return 0;
	}

      _xend();
      return 1;
    }

  return __atomic_exchange_n(&lock->lock, TAS_LOCKED, __ATOMIC_ACQUIRE);
}

static inline int
tas_lock_lock(tas_lock_t* lock) 
{
  if (_xbegin() == _XBEGIN_STARTED)
    {
      if (lock->lock == TAS_FREE)
	{
	  return 0;
	}

      _xabort(0xff);
    }
  else
    {
      while (__atomic_exchange_n(&lock->lock, TAS_LOCKED, __ATOMIC_ACQUIRE))
	{
	  PAUSE_IN();
	}
    }

  return 0;
}

static inline int
tas_lock_unlock(tas_lock_t* lock) 
{
  if (lock->lock == TAS_FREE && _xtest())
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
tas_pause_rep(int n)
{
  while (n--)
    {
      PAUSE_IN();
    }
}

/* returns 0 on success */
static inline int
tas_lock_trylock(tas_lock_t* lock) 
{
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
      tas_pause_rep(2 << t);
    }

  return __atomic_exchange_n(&lock->lock, TAS_LOCKED, __ATOMIC_ACQUIRE);
}

static inline int
tas_lock_lock(tas_lock_t* lock) 
{
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
	  tas_pause_rep(2 << t);
	}
    }


  while (__atomic_exchange_n(&lock->lock, 1, __ATOMIC_ACQUIRE))
    {
      PAUSE_IN();
    }
  return 0;
}

static inline int
tas_lock_unlock(tas_lock_t* lock) 
{
  if (lock->lock == TAS_FREE && _xtest())
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
tas_lock_init(tas_lock_t* the_lock, const pthread_mutexattr_t* a) 
{
  the_lock->lock = TAS_FREE;
  asm volatile ("mfence");
  return 0;
}

static inline int
tas_lock_destroy(tas_lock_t* the_lock) 
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
tas_cond_wait(tas_cond_t* c, tas_lock_t* m)
{
  int head = c->head;

  if (c->l != m)
    {
      if (c->l) return EINVAL;
      
      /* Atomically set mutex inside cv */
      __attribute__ ((unused)) int dummy = (uintptr_t) __sync_val_compare_and_swap(&c->l, NULL, m);
      if (c->l != m) return EINVAL;
    }
  
  tas_lock_unlock(m);
  
  sys_futex((void*) &c->head, FUTEX_WAIT_PRIVATE, head, NULL, NULL, 0);
  
  tas_lock_lock(m);
  
  return 0;
}

static inline int
tas_cond_timedwait(tas_cond_t* c, tas_lock_t* m, const struct timespec* ts)
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
  
  tas_lock_unlock(m);
  
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
  tas_lock_lock(m);
  
  return ret;
}

static inline int
tas_cond_init(tas_cond_t* c, const pthread_condattr_t* a)
{
  (void) a;
  
  c->l = NULL;
  
  /* Sequence variable doesn't actually matter, but keep valgrind happy */
  c->head = 0;
  
  return 0;
}

static inline int 
tas_cond_destroy(tas_cond_t* c)
{
  /* No need to do anything */
  (void) c;
  return 0;
}

static inline int
tas_cond_signal(tas_cond_t* c)
{
  /* We are waking someone up */
  __sync_add_and_fetch(&c->head, 1);
  
  /* Wake up a thread */
  sys_futex((void*) &c->head, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
  
  return 0;
}

static inline int
tas_cond_broadcast(tas_cond_t* c)
{
  tas_lock_t* m = c->l;
  
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
tas_cond_init(tas_cond_t* c, const pthread_condattr_t* a)
{
  c->head = 0;
  c->ticket = 0;
  c->l = NULL;
  return 0;
}

static inline int
tas_cond_destroy(tas_cond_t* c)
{
  c->head = -1;
  return 1;
}

static inline int
tas_cond_signal(tas_cond_t* c)
{
  if (c->ticket > c->head)
    {
      __sync_fetch_and_add(&c->head, 1);
    }
  return 1;
}

static inline int 
tas_cond_broadcast(tas_cond_t* c)
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
tas_cond_wait(tas_cond_t* c, tas_lock_t* l)
{
  uint32_t cond_tas = ++c->ticket;

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

  tas_lock_unlock(l);

  while (cond_tas > c->head)
    {
      PAUSE_IN();
    }

  tas_lock_lock(l);
  return 1;
}

#  if defined(__x86_64__)
static inline uint64_t
tas_getticks(void)
{
  unsigned hi, lo;
  asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
  return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}
#  endif

static inline int
tas_cond_timedwait(tas_cond_t* c, tas_lock_t* l, const struct timespec* ts)
{
  int ret = 0;

  uint32_t cond_tas = ++c->ticket;

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

  tas_lock_unlock(l);

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
  uint64_t to = tas_getticks() + ticks;

  while (cond_tas > c->head)
    {
      PAUSE_IN();
      if (tas_getticks() > to)
	{
	  ret = ETIMEDOUT;
	  break;
	}
    }

  tas_lock_lock(l);
  return ret;
}

#endif	/* USE_FUTEX_COND */


static inline int
tas_lock_timedlock(tas_lock_t* l, const struct timespec* ts)
{
  fprintf(stderr, "** warning -- pthread_mutex_timedlock not implemented\n");
  return 0;
}


#if REPLACE_MUTEX == 1
#  define pthread_mutex_init    tas_lock_init
#  define pthread_mutex_destroy tas_lock_destroy
#  define pthread_mutex_lock    tas_lock_lock
#  define pthread_mutex_timedlock tas_lock_timedlock
#  define pthread_mutex_unlock  tas_lock_unlock
#  define pthread_mutex_trylock tas_lock_trylock
#  define pthread_mutex_t       tas_lock_t
#  undef  PTHREAD_MUTEX_INITIALIZER
#  define PTHREAD_MUTEX_INITIALIZER TAS_LOCK_INITIALIZER

#  define pthread_cond_init     tas_cond_init
#  define pthread_cond_destroy  tas_cond_destroy
#  define pthread_cond_signal   tas_cond_signal
#  define pthread_cond_broadcast tas_cond_broadcast
#  define pthread_cond_wait     tas_cond_wait
#  define pthread_cond_timedwait tas_cond_timedwait
#  define pthread_cond_t        tas_cond_t
#  undef  PTHREAD_COND_INITIALIZER
#  define PTHREAD_COND_INITIALIZER TAS_COND_INITIALIZER
#endif

#ifdef __cplusplus
}
#endif

#endif


