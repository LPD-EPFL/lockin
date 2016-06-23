/*
 * File: ticket_fu_in.h
 * Author: Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Tudor David, Vasileios Trigonakis
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


#ifndef _TICKET_FU_IN_H_
#define _TICKET_FU_IN_H_

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
#ifndef __sparc__
#  include <numa.h>
#endif
#include <pthread.h>
#include <malloc.h>
#include <limits.h>

#define LOCK_IN_NAME "TICKET-FUTEX"

/* setting of the back-off based on the length of the queue */
#define TICKET_FU_BASE_WAIT 512
#define TICKET_FU_MAX_WAIT  4095
#define TICKET_FU_WAIT_NEXT 128

#define CACHE_LINE_SIZE 64
#define TICKET_FU_PAUSE()				\
  ;

typedef struct ticket_fu_lock 
{
    volatile uint32_t head;
    volatile uint32_t tail;
#if PADDING == 1
    uint8_t padding[CACHE_LINE_SIZE - 8];
#endif
} ticket_fu_lock_t;

#define TICKET_FU_LOCK_INITIALIZER { .head = 1, .tail = 0 }

typedef struct ticket_fu_cond
{
  uint32_t ticket_fu;
  volatile uint32_t head;
  ticket_fu_lock_t* l;
#if PADDING == 1
    uint8_t padding[CACHE_LINE_SIZE - 16];
#endif
} ticket_fu_cond_t;

#define TICKET_FU_COND_INITIALIZER { 0, 0, NULL }


static inline int
sys_futex(void* addr1, int op, int val1, struct timespec* timeout, void* addr2, int val3)
{
  return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}


static inline int
ticket_fu_lock_trylock(ticket_fu_lock_t* lock) 
{
  uint32_t to = lock->tail;
  if (lock->head - to == 1)
    {
      return (__sync_val_compare_and_swap(&lock->tail, to, to + 1) != to);
    }

  return 1;
}

static inline void
ticket_fu_lock_lock(ticket_fu_lock_t* lock) 
{
  uint32_t my_ticket_fu = __sync_add_and_fetch(&(lock->tail), 1);
  uint32_t head = lock->head;
  int distance = my_ticket_fu - head;
 
  if (__builtin_expect(distance == 0, 1))
    {
      return;
    }
  else if (distance > 1)
    {
      do
	{
	  sys_futex((void*) &lock->head, FUTEX_WAIT_PRIVATE, head, NULL, NULL, 0);
	  head = lock->head;
	  distance = my_ticket_fu - head;
	}
      while (distance > 1);
    }

  do
    {
      TICKET_FU_PAUSE();
      distance = my_ticket_fu - lock->head;
    }
  while (distance);
}

static inline void
ticket_fu_lock_unlock(ticket_fu_lock_t* lock) 
{
#ifdef __tile__
  MEM_BARRIER;
#endif
  asm volatile("" ::: "memory");
  lock->head++;
#warning do you want to wake them all up?
  sys_futex((void*) &lock->head, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
}


static inline int
ticket_fu_lock_init(ticket_fu_lock_t* the_lock, pthread_mutexattr_t* a) 
{
    the_lock->head=1;
    the_lock->tail=0;
    asm volatile ("mfence");
    return 0;
}

static inline int
ticket_fu_lock_destroy(ticket_fu_lock_t* the_lock) 
{
    return 0;
}


#if USE_FUTEX_COND == 1

static inline int
ticket_fu_cond_wait(ticket_fu_cond_t* c, ticket_fu_lock_t* m)
{
  int head = c->head;

  if (c->l != m)
    {
      if (c->l) return EINVAL;
      
      /* Atomically set mutex inside cv */
      __attribute__ ((unused)) int dummy = (uintptr_t) __sync_val_compare_and_swap(&c->l, NULL, m);
      if (c->l != m) return EINVAL;
    }
  
  ticket_fu_lock_unlock(m);
  
  sys_futex((void*) &c->head, FUTEX_WAIT_PRIVATE, head, NULL, NULL, 0);
  
  ticket_fu_lock_lock(m);
  
  return 0;
}

static inline int
ticket_fu_cond_init(ticket_fu_cond_t* c, pthread_condattr_t* a)
{
  (void) a;
  
  c->l = NULL;
  
  /* Sequence variable doesn't actually matter, but keep valgrind happy */
  c->head = 0;
  
  return 0;
}

static inline int 
ticket_fu_cond_destroy(ticket_fu_cond_t* c)
{
  /* No need to do anything */
  (void) c;
  return 0;
}

static inline int
ticket_fu_cond_signal(ticket_fu_cond_t* c)
{
  /* We are waking someone up */
  __sync_add_and_fetch(&c->head, 1);
  
  /* Wake up a thread */
  sys_futex((void*) &c->head, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
  
  return 0;
}

static inline int
ticket_fu_cond_broadcast(ticket_fu_cond_t* c)
{
  ticket_fu_lock_t* m = c->l;
  
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
ticket_fu_cond_init(ticket_fu_cond_t* c, pthread_condattr_t* a)
{
  c->head = 0;
  c->ticket_fu = 0;
  c->l = NULL;
  return 0;
}

static inline int
ticket_fu_cond_destroy(ticket_fu_cond_t* c)
{
  c->head = -1;
  return 1;
}

static inline int
ticket_fu_cond_signal(ticket_fu_cond_t* c)
{
  if (c->ticket_fu > c->head)
    {
      __sync_fetch_and_add(&c->head, 1);
    }
  return 1;
}

static inline int 
ticket_fu_cond_broadcast(ticket_fu_cond_t* c)
{
  if (c->ticket_fu == c->head)
    {
      return 0;
    }

  uint32_t ch, ct;
  do
    {
      ch = c->head;
      ct = c->ticket_fu;
    }
  while (__sync_val_compare_and_swap(&c->head, ch, ct) != ch);

  return 1;
}

static inline int
ticket_fu_cond_wait(ticket_fu_cond_t* c, ticket_fu_lock_t* l)
{
  uint32_t cond_ticket_fu = ++c->ticket_fu;

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

  ticket_fu_lock_unlock(l);

  while (cond_ticket_fu > c->head)
    {
      TICKET_FU_PAUSE();
    }

  ticket_fu_lock_lock(l);
  return 1;
}

#endif

#define REPLACE_MUTEX 1
#if REPLACE_MUTEX == 1
#  define pthread_mutex_init    ticket_fu_lock_init
#  define pthread_mutex_destroy ticket_fu_lock_destroy
#  define pthread_mutex_lock    ticket_fu_lock_lock
#  define pthread_mutex_unlock  ticket_fu_lock_unlock
#  define pthread_mutex_trylock ticket_fu_lock_trylock
#  define pthread_mutex_t       ticket_fu_lock_t
#  undef  PTHREAD_MUTEX_INITIALIZER
#  define PTHREAD_MUTEX_INITIALIZER TICKET_FU_LOCK_INITIALIZER

#  define pthread_cond_init     ticket_fu_cond_init
#  define pthread_cond_destroy  ticket_fu_cond_destroy
#  define pthread_cond_signal   ticket_fu_cond_signal
#  define pthread_cond_broadcast ticket_fu_cond_broadcast
#  define pthread_cond_wait     ticket_fu_cond_wait
#  define pthread_cond_t        ticket_fu_cond_t
#  undef  PTHREAD_COND_INITIALIZER
#  define PTHREAD_COND_INITIALIZER TICKET_FU_COND_INITIALIZER
#endif

#ifdef __cplusplus
}
#endif

#endif


