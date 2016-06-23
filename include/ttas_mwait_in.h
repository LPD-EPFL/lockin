/*
 * File: tas_mwait_in.h
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


#ifndef _TAS_MWAIT_IN_H_
#define _TAS_MWAIT_IN_H_

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
#  define PADDING        1        /* padd locks/conditionals to cache-line */
#endif
#if !defined(LOCK_IN_COOP)
#  define TAS_MWAIT_COOP       0	/* spin for TAS_MWAIT_MAX_SPINS before calling */
#else
#  define TAS_MWAIT_COOP       LOCK_IN_COOP
#endif
#if !defined(LOCK_IN_MAX_SPINS)
#  define TAS_MWAIT_MAX_SPINS    256	/* sched_yield() to yield the cpu to others */
#else
#  define TAS_MWAIT_MAX_SPINS LOCK_IN_MAX_SPINS
#endif
#define FREQ_CPU_GHZ     2.8
#define REPLACE_MUTEX    0	/* ovewrite the pthread_[mutex|cond] functions */

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


#if TAS_MWAIT_LAT == 1
#  define TAS_MWAIT_LAT_START()			\
  volatile ticks __lat_s = getticks();
#  define TAS_MWAIT_LAT_STOP(l)			\
  volatile ticks __lat_e = getticks();		\
  l = __lat_e - __lat_s
#else
#  define TAS_MWAIT_LAT_START()		     
#  define TAS_MWAIT_LAT_STOP(l)		       
#endif


#define TAS_MWAIT_FREE   0
#define TAS_MWAIT_LOCKED 1

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

typedef struct tas_mwait_lock 
{
  volatile uint32_t lock;
#if PADDING == 1
  uint8_t padding[CACHE_LINE_SIZE - sizeof(uint32_t)];
#endif
} tas_mwait_lock_t;

#define TAS_MWAIT_LOCK_INITIALIZER { .lock = TAS_MWAIT_FREE }


/* ******************************************************************************** */
#if USE_HLE == 0 || !defined(RTM) /* no hle */
/* ******************************************************************************** */

/* returns 0 on success */
  static inline int
  tas_mwait_lock_trylock(tas_mwait_lock_t* lock) 
  {
    return (swap_uint32(&lock->lock, TAS_MWAIT_LOCKED));
  }

  static inline int
  tas_mwait_lock_lock(tas_mwait_lock_t* lock, int fd, size_t n_lock, ticks* lat) 
  {
#if TAS_MWAIT_COOP == 1
#error coop not implemented

#else

    int i;
    for (i = 0; i < 2; i++)
      {
	if(!lock->lock && !(swap_uint32(&lock->lock, TAS_MWAIT_LOCKED)))
	  {
	    return 0;
	  }
	PAUSE_IN();
      }

    do
      {
	/* printf(":: monitor / mwait\n"); */
	TAS_MWAIT_LAT_START();
	UNUSED int __d = read(fd, NULL, n_lock);
	printf(" ~~ woke up\n");
	TAS_MWAIT_LAT_STOP(*lat);
      }
    while (swap_uint32(&lock->lock, TAS_MWAIT_LOCKED));


#endif
    return 0;
  }

static inline int
tas_mwait_lock_unlock(tas_mwait_lock_t* lock) 
{
  asm volatile("" ::: "memory");
  lock->lock = TAS_MWAIT_FREE;
  return 0;
}

/* ******************************************************************************** */
#elif USE_HLE == 1		/* plain hle */
/* ******************************************************************************** */

/* returns 0 on success */
static inline int
tas_mwait_lock_trylock(tas_mwait_lock_t* lock) 
{
  return (swap_uint32_hle_acq(&lock->lock, TAS_MWAIT_LOCKED));
}

static inline int
tas_mwait_lock_lock(tas_mwait_lock_t* lock) 
{
  while (swap_uint32_hle_acq(&lock->lock, TAS_MWAIT_LOCKED))
    {
      PAUSE_IN();
    }
  return 0;
}

static inline int
tas_mwait_lock_unlock(tas_mwait_lock_t* lock) 
{
  swap_uint32_hle_rls(&lock->lock, TAS_MWAIT_FREE);
  return 0;
}

/* ******************************************************************************** */
#elif USE_HLE == 2		/* rtm-based hle (pessimistic) */
/* ******************************************************************************** */

/* returns 0 on success */
static inline int
tas_mwait_lock_trylock(tas_mwait_lock_t* lock) 
{
  if (_xbegin() == _XBEGIN_STARTED)
    {
      if (lock->lock == TAS_MWAIT_FREE)
	{
	  return 0;
	}

      _xend();
      return 1;
    }

  return __atomic_exchange_n(&lock->lock, TAS_MWAIT_LOCKED, __ATOMIC_ACQUIRE);
}

static inline int
tas_mwait_lock_lock(tas_mwait_lock_t* lock) 
{
  if (_xbegin() == _XBEGIN_STARTED)
    {
      if (lock->lock == TAS_MWAIT_FREE)
	{
	  return 0;
	}

      _xabort(0xff);
    }
  else
    {
      while (__atomic_exchange_n(&lock->lock, TAS_MWAIT_LOCKED, __ATOMIC_ACQUIRE))
	{
	  PAUSE_IN();
	}
    }

  return 0;
}

static inline int
tas_mwait_lock_unlock(tas_mwait_lock_t* lock) 
{
  if (lock->lock == TAS_MWAIT_FREE && _xtest())
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
tas_mwait_pause_rep(int n)
{
  while (n--)
    {
      PAUSE_IN();
    }
}

/* returns 0 on success */
static inline int
tas_mwait_lock_trylock(tas_mwait_lock_t* lock) 
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
      tas_mwait_pause_rep(2 << t);
    }

  return __atomic_exchange_n(&lock->lock, TAS_MWAIT_LOCKED, __ATOMIC_ACQUIRE);
}

static inline int
tas_mwait_lock_lock(tas_mwait_lock_t* lock) 
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
	  tas_mwait_pause_rep(2 << t);
	}
    }


  while (__atomic_exchange_n(&lock->lock, 1, __ATOMIC_ACQUIRE))
    {
      PAUSE_IN();
    }
  return 0;
}

static inline int
tas_mwait_lock_unlock(tas_mwait_lock_t* lock) 
{
  if (lock->lock == TAS_MWAIT_FREE && _xtest())
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
tas_mwait_lock_init(tas_mwait_lock_t* the_lock, const pthread_mutexattr_t* a) 
{
  the_lock->lock = TAS_MWAIT_FREE;
  asm volatile ("mfence");
  return 0;
}

static inline int
tas_mwait_lock_destroy(tas_mwait_lock_t* the_lock) 
{
    return 0;
}

static inline int
tas_mwait_lock_timedlock(tas_mwait_lock_t* l, const struct timespec* ts)
{
  fprintf(stderr, "** warning -- pthread_mutex_timedlock not implemented\n");
  return 0;
}


#if REPLACE_MUTEX == 1
#  define pthread_mutex_init    tas_mwait_lock_init
#  define pthread_mutex_destroy tas_mwait_lock_destroy
#  define pthread_mutex_lock    tas_mwait_lock_lock
#  define pthread_mutex_timedlock tas_mwait_lock_timedlock
#  define pthread_mutex_unlock  tas_mwait_lock_unlock
#  define pthread_mutex_trylock tas_mwait_lock_trylock
#  define pthread_mutex_t       tas_mwait_lock_t
#  undef  PTHREAD_MUTEX_INITIALIZER
#  define PTHREAD_MUTEX_INITIALIZER TAS_MWAIT_LOCK_INITIALIZER

#  define pthread_cond_init     tas_mwait_cond_init
#  define pthread_cond_destroy  tas_mwait_cond_destroy
#  define pthread_cond_signal   tas_mwait_cond_signal
#  define pthread_cond_broadcast tas_mwait_cond_broadcast
#  define pthread_cond_wait     tas_mwait_cond_wait
#  define pthread_cond_timedwait tas_mwait_cond_timedwait
#  define pthread_cond_t        tas_mwait_cond_t
#  undef  PTHREAD_COND_INITIALIZER
#  define PTHREAD_COND_INITIALIZER TAS_MWAIT_COND_INITIALIZER
#endif

#ifdef __cplusplus
}
#endif

#endif


