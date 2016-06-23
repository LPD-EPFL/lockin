/*
 * File: ttas_shared_in.h
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


#ifndef _TTAS_SHARED_IN_H_
#define _TTAS_SHARED_IN_H_

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
#if !defined(PADDING)
#  define PADDING         1        /* padd locks/conditionals to cache-line */
#endif
#define TTAS_SHARED_COOP      1	/* spin for TTAS_SHARED_MAX_SPINS before calling */
#define TTAS_SHARED_MAX_SPINS 1024	/* sched_yield() to yield the cpu to others */
#define FREQ_CPU_GHZ      2.8
#define REPLACE_MUTEX     1	/* ovewrite the pthread_[mutex|cond] functions */
/* ******************************************************************************** */


#define TTAS_SHARED_FREE    0
#define TTAS_SHARED_WLOCKED 0xFF000000

#define CACHE_LINE_SIZE 64
#if !defined(PAUSE_IN)
#  define PAUSE_IN()			\
  ;
#endif

typedef struct ttas_shared_lock 
{
  union
  {
    struct
    {
      volatile uint16_t nr;
      volatile uint8_t padding;
      volatile uint8_t w;
    } rw;
    volatile uint32_t val;
  } lock;

#if PADDING == 1
  uint8_t padding[CACHE_LINE_SIZE - sizeof(uint32_t)];
#endif
} ttas_shared_lock_t;

#define TTAS_SHARED_LOCK_INITIALIZER { .lock.val = TTAS_SHARED_FREE }

typedef struct ttas_cond
{
  uint32_t ticket;
  volatile uint32_t head;
  ttas_shared_lock_t* l;
#if PADDING == 1
    uint8_t padding[CACHE_LINE_SIZE - 16];
#endif
} ttas_cond_t;

#define TTAS_COND_INITIALIZER { 0, 0, NULL }


/* returns 0 on success */
static inline int
ttas_shared_lock_tryrlock(ttas_shared_lock_t* lock) 
{
  while (lock->lock.rw.w == TTAS_SHARED_FREE)
    {
      uint32_t lock_old = lock->lock.val & TTAS_SHARED_FREE;
      uint32_t lock_new = lock_old + 1;
      uint32_t cas = __sync_val_compare_and_swap(&lock->lock.val, lock_old, lock_new);
      if (cas == lock_old)
	{
	  return 0;
	}
      else if (cas & TTAS_SHARED_WLOCKED) /* we have a writer in the meanime! */
	{
	  break;
	}
    }
  return 1;
}

/* returns 0 on success */
static inline int
ttas_shared_lock_trywlock(ttas_shared_lock_t* lock) 
{
  if (lock->lock.val != TTAS_SHARED_FREE)
    {
      return 1;
    }
  return (__sync_val_compare_and_swap(&lock->lock.val, TTAS_SHARED_FREE, TTAS_SHARED_WLOCKED) != TTAS_SHARED_FREE);
}

static inline int
ttas_shared_lock_trylock(ttas_shared_lock_t* lock) 
{
  return ttas_shared_lock_trywlock(lock);
}


static inline int
ttas_shared_lock_rlock(ttas_shared_lock_t* lock) 
{
  (int) __sync_add_and_fetch(&lock->lock.val, 1);
#if TTAS_COOP == 1
  size_t spins = 0;
  while (lock->lock.rw.w != TTAS_SHARED_FREE)
    {
      PAUSE_IN();
      if (spins++ == TTAS_MAX_SPINS << 2)
	{
	  sched_yield();
	  spins = 0;
	}
    }
#else
  while (lock->lock.rw.w != TTAS_SHARED_FREE)
    {
      PAUSE_IN();
    }
#endif
  return 0;
}

static inline int
ttas_shared_lock_wlock(ttas_shared_lock_t* lock) 
{
#if TTAS_COOP == 1
  size_t spins = 0;
  do
    {
      while (lock->lock.val != TTAS_SHARED_FREE)
	{
	  PAUSE_IN();
	  if (spins++ == TTAS_MAX_SPINS)
	    {
	      sched_yield();
	      spins = 0;
	    }
	}
    }
  while (__sync_val_compare_and_swap(&lock->lock.val, TTAS_SHARED_FREE, TTAS_SHARED_WLOCKED) != TTAS_SHARED_FREE);
#else
  do
    {
      while (lock->lock.val != TTAS_SHARED_FREE)
	{
	  PAUSE_IN();
	}
    }
  while (__sync_val_compare_and_swap(&lock->lock.val, TTAS_SHARED_FREE, TTAS_SHARED_WLOCKED) != TTAS_SHARED_FREE);
#endif
  return 0;
}

static inline int
ttas_shared_lock_lock(ttas_shared_lock_t* lock) 
{
  return ttas_shared_lock_wlock(lock);
}


static inline int
ttas_shared_lock_unlock(ttas_shared_lock_t* lock) 
{
  asm volatile("" ::: "memory");
  if (lock->lock.rw.w != TTAS_SHARED_FREE)
    {
      lock->lock.rw.w = TTAS_SHARED_FREE;
    }
  else if (lock->lock.rw.nr > 0)
    {
      (int) __sync_sub_and_fetch(&lock->lock.val, 1);
    }
  return 0;
}


static inline int
ttas_shared_lock_init(ttas_shared_lock_t* the_lock, const pthread_mutexattr_t* a) 
{
  the_lock->lock.val = TTAS_SHARED_FREE;
  asm volatile ("mfence");
  return 0;
}

static inline int
ttas_shared_lock_destroy(ttas_shared_lock_t* the_lock) 
{
    return 0;
}

static inline int
sys_futex(void* addr1, int op, int val1, struct timespec* timeout, void* addr2, int val3)
{
  return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}

static inline int
ttas_cond_wait(ttas_cond_t* c, ttas_shared_lock_t* m)
{
  int head = c->head;

  if (c->l != m)
    {
      if (c->l) return EINVAL;
      
      /* Atomically set mutex inside cv */
      __attribute__ ((unused)) int dummy = (uintptr_t) __sync_val_compare_and_swap(&c->l, NULL, m);
      if (c->l != m) return EINVAL;
    }
  
  ttas_shared_lock_unlock(m);
  
  sys_futex((void*) &c->head, FUTEX_WAIT_PRIVATE, head, NULL, NULL, 0);
  
  ttas_shared_lock_lock(m);
  
  return 0;
}

static inline int
ttas_cond_timedwait(ttas_cond_t* c, ttas_shared_lock_t* m, const struct timespec* ts)
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
  
  ttas_shared_lock_unlock(m);
  
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
  ttas_shared_lock_lock(m);
  
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
  sys_futex((void*) &c->head, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
  
  return 0;
}

static inline int
ttas_cond_broadcast(ttas_cond_t* c)
{
  ttas_shared_lock_t* m = c->l;
  
  /* No mutex means that there are no waiters */
  if (!m) return 0;
  
  /* We are waking everyone up */
  __sync_add_and_fetch(&c->head, 1);
  
  /* Wake one thread, and requeue the rest on the mutex */
  sys_futex((void*) &c->head, FUTEX_REQUEUE_PRIVATE, INT_MAX, NULL, NULL, 0);
  
  return 0;
}

static inline int
ttas_shared_lock_timedlock(ttas_shared_lock_t* l, const struct timespec* ts)
{
  fprintf(stderr, "** warning -- pthread_mutex_timedlock not implemented\n");
  return 0;
}



#if REPLACE_MUTEX == 1
#  define pthread_mutex_init    ttas_shared_lock_init
#  define pthread_mutex_destroy ttas_shared_lock_destroy
#  define pthread_mutex_lock    ttas_shared_lock_lock
#  define pthread_mutex_timedlock ttas_shared_lock_timedlock
#  define pthread_mutex_unlock  ttas_shared_lock_unlock
#  define pthread_mutex_trylock ttas_shared_lock_trylock
#  define pthread_mutex_t       ttas_shared_lock_t
#  undef  PTHREAD_MUTEX_INITIALIZER
#  define PTHREAD_MUTEX_INITIALIZER TTAS_SHARED_LOCK_INITIALIZER

#  define pthread_cond_init     ttas_cond_init
#  define pthread_cond_destroy  ttas_cond_destroy
#  define pthread_cond_signal   ttas_cond_signal
#  define pthread_cond_broadcast ttas_cond_broadcast
#  define pthread_cond_wait     ttas_cond_wait
#  define pthread_cond_timedwait ttas_cond_timedwait
#  define pthread_cond_t        ttas_cond_t
#  undef  PTHREAD_COND_INITIALIZER
#  define PTHREAD_COND_INITIALIZER TTAS_COND_INITIALIZER

#  define pthread_rwlock_init    ttas_shared_lock_init
#  define pthread_rwlock_destroy ttas_shared_lock_destroy
#  define pthread_rwlock_rdlock  ttas_shared_lock_rlock
#  define pthread_rwlock_wrlock  ttas_shared_lock_wlock
#  define pthread_rwlock_unlock  ttas_shared_lock_unlock
#  define pthread_rwlock_tryrdlock ttas_shared_lock_tryrlock
#  define pthread_rwlock_trywrlock ttas_shared_lock_trywlock
#  define pthread_rwlock_t       ttas_shared_lock_t
#  undef  PTHREAD_RWLOCK_INITIALIZER
#  define PTHREAD_RWLOCK_INITIALIZER TTAS_SHARED_LOCK_INITIALIZER
#endif

#ifdef __cplusplus
}
#endif

#endif


