#ifndef _LADAP_IN_H_
#define _LADAP_IN_H_

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
#define PADDING        1        /* padd locks/conditionals to cache-line */
#define FREQ_CPU_GHZ   2.8	/* core frequency in GHz */
#define REPLACE_MUTEX  1	/* ovewrite the pthread_[mutex|cond] functions */

#define LADAP_PRINT                  0

#define LADAP_SPIN_TRIES_LOCK_MAX    16386
#define LADAP_SPIN_TRIES_UNLOCK_MAX  128
#define LADAP_CONTENTION_MAX         4
#define LADAP_FAIR_FUTEX_EVERY       8192

  /* ******************************************************************************** */


#if !defined(PAUSE_IN)
#  define PAUSE_IN()				\
  ;
#endif

  //Swap uint32_t
  static inline uint32_t
  swap_uint32(volatile uint32_t* target,  uint32_t x)
  {
    asm volatile("xchgl %0,%1"
		 :"=r" ((uint32_t) x)
		 :"m" (*(volatile uint32_t *)target), "0" ((uint32_t) x)
		 :"memory");

    return x;
  }

  //Swap uint8_t
  static inline uint8_t
  swap_uint8(volatile uint8_t* target,  uint8_t x) 
  {
    asm volatile("xchgb %0,%1"
		 :"=r" ((uint8_t) x)
		 :"m" (*(volatile uint8_t *)target), "0" ((uint8_t) x)
		 :"memory");

    return x;
  }
#define cmpxchg(a, b, c) __sync_val_compare_and_swap(a, b, c)
#define xchg_32(a, b)    swap_uint32((uint32_t*) a, b)
#define xchg_8(a, b)     swap_uint8((uint8_t*) a, b)
#define atomic_add(a, b) __sync_fetch_and_add(a, b)

#define CACHE_LINE_SIZE 64

  typedef struct ladap_lock
  {
    union
    {
      volatile unsigned u;
      volatile struct
      {
	volatile unsigned char locked;
	volatile unsigned char contended;
      } b;
    } l;
    volatile uint32_t contention;
    uint32_t n_spin_acq;
    uint16_t n_futex;
    
    /* #if PADDING == 1 */
    /*     uint8_t padding[CACHE_LINE_SIZE - 7 * sizeof(size_t)]; */
    /* #endif */
  } ladap_lock_t;

#define LADAP_INITIALIZER			\
  {						\
    .l.u = 0					\
      }

  typedef struct upmutex_cond1
  {
    ladap_lock_t* m;
    int seq;
    int pad;
#if PADDING == 1
    uint8_t padding[CACHE_LINE_SIZE - 16];
#endif
  } upmutex_cond1_t;

#define UPMUTEX_COND1_INITIALIZER {NULL, 0, 0}

  static inline int
  sys_futex(void* addr1, int op, int val1, struct timespec* timeout, void* addr2, int val3)
  {
    return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
  }

  static inline int
  ladap_init(ladap_lock_t* m, const pthread_mutexattr_t* a)
  {
    (void) a;
    m->l.u = 0;
    return 0;
  }

  static inline int
  ladap_destroy(ladap_lock_t* m)
  {
    /* Do nothing */
    (void) m;
    return 0;
  }

#define ladap_unlikely(x) __builtin_expect((x), 0)

  static inline int
  ladap_lock(ladap_lock_t* m)
  {
    uint32_t contention = __sync_fetch_and_add(&m->contention, 1);
    if (contention > LADAP_CONTENTION_MAX)
      {
	__sync_fetch_and_sub(&m->contention, 1);
	goto sleep;
      }

    if (!m->l.b.locked && !xchg_8(&m->l.b.locked, 1))
      {
	m->n_spin_acq++;
	goto locked;
      }

    size_t i;
    do
      {
	/* Try to grab lock */
	for (i = 1; i <= LADAP_SPIN_TRIES_LOCK_MAX; i++)
	  {
	    if (!m->l.b.locked && !xchg_8(&m->l.b.locked, 1))
	      {
		m->n_spin_acq++;
		goto locked;
	      }

	    PAUSE_IN();
	  }

      sleep:
	/* Have to sleep */
	if (!(xchg_32(&m->l.u, 257) & 1))
	  {
	    break;
	  }
	m->n_futex++;
	sys_futex(m, FUTEX_WAIT_PRIVATE, 257, NULL, NULL, 0);
	__sync_fetch_and_add(&m->contention, 1);
	m->n_futex--;
	if (!(xchg_32(&m->l.u, 257) & 1))
	  {
	    break;
	  }
      }
    while(1);

  locked:
    __sync_fetch_and_sub(&m->contention, 1);
    return 0;
  }


  static inline int
  ladap_unlock(ladap_lock_t* m)
  {
    /* Locked and not contended */
    if ((m->l.u == 1) && (cmpxchg(&m->l.u, 1, 0) == 1)) 
      {
	return 0;
      }

    /* Unlock */
    m->l.b.locked = 0;

    asm volatile ("");

    unsigned int i;
    for (i = 0; i < LADAP_SPIN_TRIES_UNLOCK_MAX; i++)
      {
	if (m->l.b.locked) 
	  {
	    if (ladap_unlikely(m->n_spin_acq > LADAP_FAIR_FUTEX_EVERY && m->n_futex))
	      {
		m->n_spin_acq = 0;
		break;
	      }
	    return 0;
	  }

	PAUSE_IN();
      }

    /* We need to wake someone up */
    m->l.b.contended = 0;
    sys_futex(m, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);

    return 0;
  }

  static inline int
  ladap_lock_trylock(ladap_lock_t* m)
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
    sys_futex(&c->seq, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
  
    return 0;
  }

  static inline int
  upmutex_cond1_broadcast(upmutex_cond1_t* c)
  {
    ladap_lock_t* m = c->m;
  
    /* No mutex means that there are no waiters */
    if (!m) return 0;
  
    /* We are waking everyone up */
    atomic_add(&c->seq, 1);
  
    /* Wake one thread, and requeue the rest on the mutex */
    sys_futex(&c->seq, FUTEX_REQUEUE_PRIVATE, 1, (struct timespec *) INT_MAX, m, 0);
  
    return 0;
  }

  static inline int
  upmutex_cond1_wait(upmutex_cond1_t* c, ladap_lock_t* m)
  {
    int seq = c->seq;

    if (c->m != m)
      {
	if (c->m) return EINVAL;
	/* Atomically set mutex inside cv */
	__attribute__ ((unused)) int dummy = (uintptr_t) cmpxchg(&c->m, NULL, m);
	if (c->m != m) return EINVAL;
      }
  
    ladap_unlock(m);
  
    sys_futex(&c->seq, FUTEX_WAIT_PRIVATE, seq, NULL, NULL, 0);
  
    while (xchg_32(&m->l.b.locked, 257) & 1)
      {
	sys_futex(m, FUTEX_WAIT_PRIVATE, 257, NULL, NULL, 0);
      }
  
    return 0;
  }


  static inline int
  ladap_cond_timedwait(upmutex_cond1_t* c, ladap_lock_t* m, const struct timespec* ts)
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
  
    ladap_unlock(m);

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
  
    sys_futex(&c->seq, FUTEX_WAIT_PRIVATE, seq, &rt, NULL, 0);
  
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
	sys_futex(m, FUTEX_WAIT_PRIVATE, 257, NULL, NULL, 0);
      }

    return ret;
  }

  static inline int
  ladap_lock_timedlock(ladap_lock_t* l, const struct timespec* ts)
  {
    fprintf(stderr, "** warning -- pthread_mutex_timedlock not implemented\n");
    return 0;
  }


#if REPLACE_MUTEX == 1
#  define pthread_mutex_init    ladap_init
#  define pthread_mutex_destroy ladap_destroy
#  define pthread_mutex_lock    ladap_lock
#  define pthread_mutex_timedlock ladap_lock_timedlock
#  define pthread_mutex_unlock  ladap_unlock
#  define pthread_mutex_trylock ladap_lock_trylock
#  define pthread_mutex_t       ladap_lock_t
#  undef  PTHREAD_MUTEX_INITIALIZER
#  define PTHREAD_MUTEX_INITIALIZER LADAP_INITIALIZER

#  define pthread_cond_init     upmutex_cond1_init
#  define pthread_cond_destroy  upmutex_cond1_destroy
#  define pthread_cond_signal   upmutex_cond1_signal
#  define pthread_cond_broadcast upmutex_cond1_broadcast
#  define pthread_cond_timedwait ladap_cond_timedwait
#  define pthread_cond_wait     upmutex_cond1_wait
#  define pthread_cond_t        upmutex_cond1_t
#  undef  PTHREAD_COND_INITIALIZER
#  define PTHREAD_COND_INITIALIZER UPMUTEX_COND1_INITIALIZER
#endif

#ifdef __cplusplus
}
#endif

#endif


