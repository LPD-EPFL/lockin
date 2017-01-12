/*
 * File: gls_in.h
 * Authors: Jelena Antic <jelena.antic@epfl.ch>
 *          Georgios Chatzopoulos <georgios.chatzopoulos@epfl.ch>
 *          Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description: 
 *      An implementation of a Generic Locking Service that uses
 *      cache-line hash table to store locked memory locations.
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Jelena Antic, Georgios Chatzopoulos, Vasileios Trigonakis
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

#ifndef _GLS_IN_H_
#define _GLS_IN_H_

#include <stdint.h>
#include <stdlib.h>

#include "atomic_ops.h"
#include "gls.h"

#define xstr(s) str(s)
#define str(s) #s

#define LOCK_IN_NAME "GLS" xstr(GLS_LOCK_IN_TYPE_DEFAULT)
#define REPLACE_MUTEX    1    /* ovewrite the pthread_[mutex|cond] functions */

#define GLS_GLK   0
#define GLS_TTAS    2
#define GLS_MCS     3
#define GLS_MUTEX   4
#define GLS_TICKET  5
#define GLS_TAS     6
#define GLS_TAS_IN  7

#ifndef GLS_LOCK_IN_TYPE_DEFAULT
#  define GLS_LOCK_IN_TYPE_DEFAULT GLS_GLK
#endif

typedef int gls_mutex_api_lock_t;
typedef int gls_mutex_api_cond_t;

static inline int
gls_mutex_api_init(gls_mutex_api_lock_t *lock, const pthread_mutexattr_t* a)
{
  gls_lock_init(lock);;
  return 0;
}

static inline int gls_mutex_api_destroy(gls_mutex_api_lock_t *lock) {
	return 0;
}

static inline int gls_mutex_api_lock(gls_mutex_api_lock_t *lock) {
#if GLS_LOCK_IN_TYPE_DEFAULT == GLS_GLK 
  gls_lock(lock);
#elif GLS_LOCK_IN_TYPE_DEFAULT == GLS_TTAS
  gls_lock_ttas(lock);
#elif GLS_LOCK_IN_TYPE_DEFAULT == GLS_TICKET
  gls_lock_ticket(lock);
#elif GLS_LOCK_IN_TYPE_DEFAULT == GLS_MCS
  gls_lock_mcs(lock);
#elif GLS_LOCK_IN_TYPE_DEFAULT == GLS_MUTEX
  gls_lock_mutex(lock);
#elif GLS_LOCK_IN_TYPE_DEFAULT == GLS_TAS
  gls_lock_tas(lock);
#elif GLS_LOCK_IN_TYPE_DEFAULT == GLS_TAS_IN
  gls_lock_tas_in(lock);
#else
#  error Incorrect GLS_LOCK_IN_TYPE_DEFAULT
#endif	
	return 0;
}

static inline int gls_mutex_api_unlock(gls_mutex_api_lock_t *lock) {
#if GLS_LOCK_IN_TYPE_DEFAULT == GLS_GLK 
  gls_unlock(lock);
#elif GLS_LOCK_IN_TYPE_DEFAULT == GLS_TTAS
  gls_unlock_ttas(lock);
#elif GLS_LOCK_IN_TYPE_DEFAULT == GLS_TICKET
  gls_unlock_ticket(lock);
#elif GLS_LOCK_IN_TYPE_DEFAULT == GLS_MCS
  gls_unlock_mcs(lock);
#elif GLS_LOCK_IN_TYPE_DEFAULT == GLS_MUTEX
  gls_unlock_mutex(lock);
#elif GLS_LOCK_IN_TYPE_DEFAULT == GLS_TAS
  gls_unlock_tas(lock);
#elif GLS_LOCK_IN_TYPE_DEFAULT == GLS_TAS_IN
  gls_unlock_tas_in(lock);
#else
#  error Incorrect GLS_LOCK_IN_TYPE_DEFAULT
#endif	
	return 0;
}

static inline int gls_mutex_api_trylock(gls_mutex_api_lock_t *lock) {
#if GLS_LOCK_IN_TYPE_DEFAULT == GLS_GLK 
  gls_trylock(lock);
#elif GLS_LOCK_IN_TYPE_DEFAULT == GLS_TTAS
  gls_trylock_ttas(lock);
#elif GLS_LOCK_IN_TYPE_DEFAULT == GLS_TICKET
  gls_trylock_ticket(lock);
#elif GLS_LOCK_IN_TYPE_DEFAULT == GLS_MCS
  gls_trylock_mcs(lock);
#elif GLS_LOCK_IN_TYPE_DEFAULT == GLS_MUTEX
  gls_trylock_mutex(lock);
#elif GLS_LOCK_IN_TYPE_DEFAULT == GLS_TAS
  gls_trylock_tas(lock);
#elif GLS_LOCK_IN_TYPE_DEFAULT == GLS_TAS_IN
  gls_trylock_tas_in(lock);
#endif	
  return 0;
}

static inline int gls_mutex_api_cond_init(gls_mutex_api_cond_t* c, const pthread_condattr_t* a) {
	return 0;
}

static inline int gls_mutex_api_cond_destroy(gls_mutex_api_cond_t* c) {
	return 0;
}

static inline int gls_mutex_api_cond_signal(gls_mutex_api_cond_t* c) {
	return 0;
}

static inline int gls_mutex_api_cond_broadcast(gls_mutex_api_cond_t* c) {
	return 0;
}

static inline int gls_mutex_api_cond_wait(gls_mutex_api_cond_t* c, gls_mutex_api_lock_t* m) {
	return 0;
}

static inline int gls_mutex_api_cond_timedwait(gls_mutex_api_cond_t* c, gls_mutex_api_lock_t* m, const struct timespec* ts) {
	return 0;
}


static inline int gls_mutex_api_timedlock(gls_mutex_api_lock_t* l, const struct timespec* ts)
{
  fprintf(stderr, "** warning -- pthread_mutex_timedlock not implemented\n");
  return 0;
}


#if REPLACE_MUTEX == 1
#  define pthread_mutex_init(a, b)    gls_mutex_api_init((gls_mutex_api_lock_t*) a, b)
#  define pthread_mutex_destroy gls_mutex_api_destroy
#  define pthread_mutex_lock    gls_mutex_api_lock
#  define pthread_mutex_timedlock gls_mutex_api_timedlock
#  define pthread_mutex_unlock  gls_mutex_api_unlock
#  define pthread_mutex_trylock gls_mutex_api_trylock
#  define pthread_mutex_t       gls_mutex_api_lock_t
#  undef  PTHREAD_MUTEX_INITIALIZER
#  define PTHREAD_MUTEX_INITIALIZER GLS_DEBUG_LOCK_INIT

#  define pthread_cond_init      gls_cond_init
#  define pthread_cond_destroy   gls_cond_destroy
#  define pthread_cond_signal    gls_cond_signal
#  define pthread_cond_broadcast gls_cond_broadcast
#  define pthread_cond_wait      gls_cond_wait
#  define pthread_cond_timedwait gls_cond_timedwait
#  define pthread_cond_t         gls_cond_t
#  undef  PTHREAD_COND_INITIALIZER
#  define PTHREAD_COND_INITIALIZER GLS_COND_INITIALIZER
#endif

typedef struct gls_cond
{
  uint32_t gls;
  volatile uint32_t head;
  gls_mutex_api_lock_t* l;
#if PADDING == 1
  uint8_t padding[CACHE_LINE_SIZE - 16];
#endif
} gls_cond_t;

#define GLS_COND_INITIALIZER { 0, 0, NULL }

static inline int
sys_futex_gls(void* addr1, int op, int val1, struct timespec* timeout, void* addr2, int val3)
{
  return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}

static inline int
gls_cond_wait(gls_cond_t* c, gls_mutex_api_lock_t* m)
{
  int head = c->head;

  if (c->l != m)
    {
      if (c->l) return EINVAL;
      
      /* Atomically set mutex inside cv */
      __attribute__ ((unused)) int dummy = (uintptr_t) __sync_val_compare_and_swap(&c->l, NULL, m);
      if (c->l != m) return EINVAL;
    }
  
  gls_unlock(m);
  
  sys_futex_gls((void*) &c->head, FUTEX_WAIT_PRIVATE, head, NULL, NULL, 0);
  
  gls_lock(m);
  
  return 0;
}

static inline int
gls_cond_timedwait(gls_cond_t* c, gls_mutex_api_lock_t* m, const struct timespec* ts)
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
  
  gls_unlock(m);
  
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

  sys_futex_gls((void*) &c->head, FUTEX_WAIT_PRIVATE, head, &rt, NULL, 0);
  
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
  gls_lock(m);
  
  return ret;
}

static inline int
gls_cond_init(gls_cond_t* c, const pthread_condattr_t* a)
{
  (void) a;
  
  c->l = NULL;
  
  /* Sequence variable doesn't actually matter, but keep valgrind happy */
  c->head = 0;
  
  return 0;
}

static inline int 
gls_cond_destroy(gls_cond_t* c)
{
  /* No need to do anything */
  (void) c;
  return 0;
}

static inline int
gls_cond_signal(gls_cond_t* c)
{
  /* We are waking someone up */
  __sync_add_and_fetch(&c->head, 1);
  
  /* Wake up a thread */
  sys_futex_gls((void*) &c->head, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
  
  return 0;
}

static inline int
gls_cond_broadcast(gls_cond_t* c)
{
  gls_mutex_api_lock_t* m = c->l;
  
  /* No mutex means that there are no waiters */
  if (!m) return 0;
  
  /* We are waking everyone up */
  __sync_add_and_fetch(&c->head, 1);
  
  /* Wake one thread, and requeue the rest on the mutex */
  sys_futex_gls((void*) &c->head, FUTEX_REQUEUE_PRIVATE, INT_MAX, NULL, NULL, 0);
  
  return 0;
}

#endif
