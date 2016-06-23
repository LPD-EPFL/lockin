/*
 * File: ticket_linux_in.h
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


#ifndef _TICKET_LINUX_IN_H_
#define _TICKET_LINUX_IN_H_

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
#if !defined(LOCK_IN_COOP)
#  define TICKET_LINUX_COOP    1      /* spin for TICKET_LINUX_MAX_SPINS before calling */
#else
#  define TICKET_LINUX_COOP    LOCK_IN_COOP
#endif
#if !defined(LOCK_IN_MAX_SPINS)
#  define TICKET_LINUX_MAX_SPINS 256    /* sched_yield() to yield the cpu to others */
#else
#  define TICKET_LINUX_MAX_SPINS LOCK_IN_MAX_SPINS
#endif
#define FREQ_CPU_GHZ     2.8	/* core frequency in GHz */
#define REPLACE_MUTEX    1	/* ovewrite the pthread_[mutex|cond] functions */
  /* ******************************************************************************** */

#define CACHE_LINE_SIZE 64

#if !defined(PAUSE_IN)
#  define PAUSE_IN()				\
  ;
#endif

  typedef uint16_t u16;
  typedef uint32_t u32;

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

#define SPIN_THRESHOLD	(1 << 15)
#define TICKET_SLOWPATH_FLAG	((__ticket_t)0)
#define __TICKET_LOCK_INC	1
typedef u16 __ticket_t;
typedef u32 __ticketpair_t;

#define TICKET_LOCK_INC	((__ticket_t)__TICKET_LOCK_INC)

#define TICKET_SHIFT	(sizeof(__ticket_t) * 8)

typedef struct arch_spinlock {
	union {
		__ticketpair_t head_tail;
		struct __raw_tickets {
			__ticket_t head, tail;
		} tickets;
	};

#if PADDING == 1
    uint8_t padding[CACHE_LINE_SIZE - sizeof(uint32_t)];
#endif
} arch_spinlock_t;

  typedef arch_spinlock_t ticket_linux_lock_t;

#define __ARCH_SPIN_LOCK_UNLOCKED	{ { 0 } }
#define TICKET_LINUX_LOCK_INITIALIZER __ARCH_SPIN_LOCK_UNLOCKED

  typedef struct ticket_linux_cond
  {
  uint32_t ticket_linux;
    volatile uint32_t head;
    ticket_linux_lock_t* l;
#if PADDING == 1
    uint8_t padding[CACHE_LINE_SIZE - 16];
#endif
  } ticket_linux_cond_t;

#define TICKET_LINUX_COND_INITIALIZER { 0, 0, NULL }

#define __X86_CASE_B 1
#define __X86_CASE_W 2
#define __X86_CASE_L 4
#define __X86_CASE_Q 8

#define __xchg_op(ptr, arg, op, lock)					\
	({								\
	        __typeof__ (*(ptr)) __ret = (arg);			\
		switch (sizeof(*(ptr))) {				\
		case __X86_CASE_B:					\
			asm volatile (lock #op "b %b0, %1\n"		\
				      : "+q" (__ret), "+m" (*(ptr))	\
				      : : "memory", "cc");		\
			break;						\
		case __X86_CASE_W:					\
			asm volatile (lock #op "w %w0, %1\n"		\
				      : "+r" (__ret), "+m" (*(ptr))	\
				      : : "memory", "cc");		\
			break;						\
		case __X86_CASE_L:					\
			asm volatile (lock #op "l %0, %1\n"		\
				      : "+r" (__ret), "+m" (*(ptr))	\
				      : : "memory", "cc");		\
			break;						\
		case __X86_CASE_Q:					\
			asm volatile (lock #op "q %q0, %1\n"		\
				      : "+r" (__ret), "+m" (*(ptr))	\
				      : : "memory", "cc");		\
			break;						\
		default:						\
		  asm volatile ("");					\
		}							\
		__ret;							\
	})

#define __xadd(ptr, inc, lock)	__xchg_op((ptr), (inc), xadd, lock)
#define xadd(ptr, inc)		__xadd((ptr), (inc), "lock ")

#define __add(ptr, inc, lock)						\
	({								\
	        __typeof__ (*(ptr)) __ret = (inc);			\
		switch (sizeof(*(ptr))) {				\
		case __X86_CASE_B:					\
			asm volatile (lock "addb %b1, %0\n"		\
				      : "+m" (*(ptr)) : "qi" (inc)	\
				      : "memory", "cc");		\
			break;						\
		case __X86_CASE_W:					\
			asm volatile (lock "addw %w1, %0\n"		\
				      : "+m" (*(ptr)) : "ri" (inc)	\
				      : "memory", "cc");		\
			break;						\
		case __X86_CASE_L:					\
			asm volatile (lock "addl %1, %0\n"		\
				      : "+m" (*(ptr)) : "ri" (inc)	\
				      : "memory", "cc");		\
			break;						\
		case __X86_CASE_Q:					\
			asm volatile (lock "addq %1, %0\n"		\
				      : "+m" (*(ptr)) : "ri" (inc)	\
				      : "memory", "cc");		\
			break;						\
		default:						\
		  asm volatile ("")					\
		}							\
		__ret;							\
	})

  static inline int
  ticket_linux_lock_trylock(ticket_linux_lock_t* lock) 
  {
    arch_spinlock_t old, new;

    old.tickets = ACCESS_ONCE(lock->tickets);
    if (old.tickets.head != (old.tickets.tail & ~TICKET_SLOWPATH_FLAG))
      return 0;

    new.head_tail = old.head_tail + (TICKET_LOCK_INC << TICKET_SHIFT);

    /* cmpxchg is a full barrier, so nothing can move before it */
    return __sync_val_compare_and_swap(&lock->head_tail, old.head_tail, new.head_tail) == old.head_tail;
  }

  static inline void 
  __ticket_lock_spinning(ticket_linux_lock_t* lock, u16 inc)
  {

  }


  static inline int
  ticket_linux_lock_lock(ticket_linux_lock_t* lock) 
  {
    register struct __raw_tickets inc = { .tail = TICKET_LOCK_INC };

    inc = xadd(&lock->tickets, inc);
    if (likely(inc.head == inc.tail))
      goto out;

    inc.tail &= ~TICKET_SLOWPATH_FLAG;
    for (;;) 
      {
	unsigned count = SPIN_THRESHOLD;

	do 
	  {
	    if (ACCESS_ONCE(lock->tickets.head) == inc.tail)
	      goto out;
	    asm volatile ("pause"); //    cpu_relax();
	  } 
	while (--count);
	__ticket_lock_spinning(lock, inc.tail);
      }
  out:	asm volatile("mfence");	/* make sure nothing creeps before the lock is taken */
    return 0;
  }

static inline void
__ticket_unlock_slowpath(arch_spinlock_t *lock, arch_spinlock_t old)
{
  printf("nothing\n");
}

  static inline int
  ticket_linux_lock_unlock(ticket_linux_lock_t* lock) 
  {
    if (TICKET_SLOWPATH_FLAG) 
      {
	arch_spinlock_t prev;

	prev = *lock;
	__sync_fetch_and_add(&lock->tickets.head, TICKET_LOCK_INC);

	/* add_smp() is a full mb() */

	if (unlikely(lock->tickets.tail & TICKET_SLOWPATH_FLAG))
	  __ticket_unlock_slowpath(lock, prev);
      } 
    else
      lock->tickets.head += TICKET_LOCK_INC;
    return 0;
  }


  static inline int
  ticket_linux_lock_init(ticket_linux_lock_t* the_lock, const pthread_mutexattr_t* a) 
  {
    the_lock->head_tail = 0;
    asm volatile ("mfence");
    return 0;
  }

  static inline int
  ticket_linux_lock_destroy(ticket_linux_lock_t* the_lock) 
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
  ticket_linux_cond_wait(ticket_linux_cond_t* c, ticket_linux_lock_t* m)
  {
    int head = c->head;

    if (c->l != m)
      {
	if (c->l) return EINVAL;
      
	/* Atomically set mutex inside cv */
	__attribute__ ((unused)) int dummy = (uintptr_t) __sync_val_compare_and_swap(&c->l, NULL, m);
	if (c->l != m) return EINVAL;
      }
  
    ticket_linux_lock_unlock(m);
  
    sys_futex((void*) &c->head, FUTEX_WAIT_PRIVATE, head, NULL, NULL, 0);
  
    ticket_linux_lock_lock(m);
  
    return 0;
  }

  static inline int
  ticket_linux_cond_timedwait(ticket_linux_cond_t* c, ticket_linux_lock_t* m, const struct timespec* ts)
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
  
    ticket_linux_lock_unlock(m);
  
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
    ticket_linux_lock_lock(m);
  
    return ret;
  }

  static inline int
  ticket_linux_cond_init(ticket_linux_cond_t* c, const pthread_condattr_t* a)
  {
    (void) a;
  
    c->l = NULL;
  
    /* Sequence variable doesn't actually matter, but keep valgrind happy */
    c->head = 0;
  
    return 0;
  }

  static inline int 
  ticket_linux_cond_destroy(ticket_linux_cond_t* c)
  {
    /* No need to do anything */
    (void) c;
    return 0;
  }

  static inline int
  ticket_linux_cond_signal(ticket_linux_cond_t* c)
  {
    /* We are waking someone up */
    __sync_add_and_fetch(&c->head, 1);
  
    /* Wake up a thread */
    sys_futex((void*) &c->head, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
  
    return 0;
  }

  static inline int
  ticket_linux_cond_broadcast(ticket_linux_cond_t* c)
  {
    ticket_linux_lock_t* m = c->l;
  
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
  ticket_linux_cond_init(ticket_linux_cond_t* c, pthread_condattr_t* a)
  {
    c->head = 0;
    c->ticket_linux = 0;
    c->l = NULL;
    return 0;
  }

  static inline int
  ticket_linux_cond_destroy(ticket_linux_cond_t* c)
  {
    c->head = -1;
    return 1;
  }

  static inline int
  ticket_linux_cond_signal(ticket_linux_cond_t* c)
  {
    if (c->ticket_linux > c->head)
      {
	__sync_fetch_and_add(&c->head, 1);
      }
    return 1;
  }

  static inline int 
  ticket_linux_cond_broadcast(ticket_linux_cond_t* c)
  {
    if (c->ticket_linux == c->head)
      {
	return 0;
      }

    uint32_t ch, ct;
    do
      {
	ch = c->head;
	ct = c->ticket_linux;
      }
    while (__sync_val_compare_and_swap(&c->head, ch, ct) != ch);

    return 1;
  }

  static inline int
  ticket_linux_cond_wait(ticket_linux_cond_t* c, ticket_linux_lock_t* l)
  {
    uint32_t cond_ticket_linux = ++c->ticket_linux;

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

    ticket_linux_lock_unlock(l);

    while (1)
      {
	int distance = cond_ticket_linux - c->head;
	if (distance == 0)
	  {
	    break;
	  }
	PAUSE_IN();
      }

    ticket_linux_lock_lock(l);
    return 1;
  }

  static inline int
  ticket_linux_cond_timedwait(ticket_linux_cond_t* c, ticket_linux_lock_t* l, const struct timespec* ts)
  {
    int ret = 0;

    uint32_t cond_ticket_linux = ++c->ticket_linux;

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

    ticket_linux_lock_unlock(l);

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
    uint64_t to = ticket_linux_getticks() + ticks;

    while (cond_ticket_linux > c->head)
      {
	PAUSE_IN();
	if (ticket_linux_getticks() > to)
	  {
	    ret = ETIMEDOUT;
	    break;
	  }
      }

    ticket_linux_lock_lock(l);
    return ret;
  }

#endif	/* USE_FUTEX_COND */

  static inline int
  ticket_linux_lock_timedlock(ticket_linux_lock_t* l, const struct timespec* ts)
  {
    fprintf(stderr, "** warning -- pthread_mutex_timedlock not implemented\n");
    return 0;
  }

#if REPLACE_MUTEX == 1
#  define pthread_mutex_init    ticket_linux_lock_init
#  define pthread_mutex_destroy ticket_linux_lock_destroy
#  define pthread_mutex_lock    ticket_linux_lock_lock
#  define pthread_mutex_timedlock ticket_linux_lock_timedlock
#  define pthread_mutex_unlock  ticket_linux_lock_unlock
#  define pthread_mutex_trylock ticket_linux_lock_trylock
#  define pthread_mutex_t       ticket_linux_lock_t
#  undef  PTHREAD_MUTEX_INITIALIZER
#  define PTHREAD_MUTEX_INITIALIZER TICKET_LINUX_LOCK_INITIALIZER

#  define pthread_cond_init     ticket_linux_cond_init
#  define pthread_cond_destroy  ticket_linux_cond_destroy
#  define pthread_cond_signal   ticket_linux_cond_signal
#  define pthread_cond_broadcast ticket_linux_cond_broadcast
#  define pthread_cond_wait     ticket_linux_cond_wait
#  define pthread_cond_timedwait ticket_linux_cond_timedwait
#  define pthread_cond_t        ticket_linux_cond_t
#  undef  PTHREAD_COND_INITIALIZER
#  define PTHREAD_COND_INITIALIZER TICKET_LINUX_COND_INITIALIZER
#endif

#ifdef __cplusplus
}
#endif

#endif


