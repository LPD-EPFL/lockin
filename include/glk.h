/*
 * File: glk.h
 * Authors: Jelena Antic <jelena.antic@epfl.ch>
 *          Georgios Chatzopoulos <georgios.chatzopoulos@epfl.ch>
 *          Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description: 
 *      An implementation of the Generic Lock (GLK), an adaptive lock that switches
 *      among the TICKET, MCS and MUTEX lock algorithms.
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

#ifndef _GLK_H_
#define _GLK_H_

#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
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

#ifndef PADDING
#  define PADDING        1        /* padd locks/conditionals to cache-line */
#endif

#if !defined(PAUSE_IN)
#  define PAUSE_IN()            \
  asm volatile ("mfence")
#endif

#include "atomic_ops.h"

#define GLK_DEBUG_PRINT              1
#define GLK_DO_ADAP                  1

#define GLK_INIT_LOCK_TYPE           TICKET_LOCK
#define GLK_INIT_LOCK_TYPE_MP        MUTEX_LOCK /* use GLK_INIT_LOCK_TYPE to use the same type
						     while MP */
#define GLK_MP_TO_LOCK               TICKET_LOCK /* MUTEX -> ?? type of lock  */
#define GLK_THREAD_CACHE             0

#define GLK_CONTENTION_RATIO_HIGH    3
#define GLK_CONTENTION_RATIO_LOW     2

#define GLK_SAMPLE_LOCK_EVERY        127
#define GLK_ADAPT_LOCK_EVERY         4095

/* overriding setting at compile time */
#if defined(GLK_ADP) && defined(GLK_ITP) && defined(GLK_SLE) && defined(GLK_ALE)
/* #  warning Overriding GLK settings  */
#  undef GLK_DO_ADAP
#  undef GLK_INIT_LOCK_TYPE
#  undef GLK_SAMPLE_LOCK_EVERY
#  undef GLK_ADAPT_LOCK_EVERY
#  define GLK_DO_ADAP                GLK_ADP
#  define GLK_INIT_LOCK_TYPE         GLK_ITP
#  define GLK_SAMPLE_LOCK_EVERY      GLK_SLE
#  define GLK_ADAPT_LOCK_EVERY       GLK_ALE
#endif

#define GLK_SAMPLE_NUM               ((GLK_ADAPT_LOCK_EVERY+1) / (GLK_SAMPLE_LOCK_EVERY+1))

#define GLK_MEASURE_SHIFT            3
#define GLK_NUM_ACQ_INIT             (GLK_ADAPT_LOCK_EVERY + 1) >> GLK_MEASURE_SHIFT

/* multiprogramming settings */
#define GLK_MP_NUM_ZERO_REQ          4
#define GLK_MP_NUM_ZERO_SHIFT        2
#define GLK_MP_NUM_ZERO_MAX          8192 // * 100 ~= 0.8 s
#define GLK_MP_SLEEP_SHIFT           3
#define GLK_MP_CHECK_PERIOD_US       100
#define GLK_MP_CHECK_THRESHOLD_HIGH  2 /* how many running threads > hw_ctx allow before switching */
#define GLK_MP_CHECK_THRESHOLD_LOW   5 /* how many running threads < hw_ctx allow before switching */
#define GLK_MP_MAX_ADAP_PER_SEC      8 
#define GLK_MP_LOW_ADAP_PER_SEC      2
#define GLK_MP_FIXED_SLEEP_MAX_LOG   5 /* sleep up to 2^GLK_MP_FIXED_SLEEP_MAX_LOG seconds */
#define GLK_MP_LOW_CONTENTION_TICKET 0 /* if TICKET has low contention, don't go to mutex */

#define TICKET_LOCK                           1
#define MCS_LOCK                              2
#define MUTEX_LOCK                          3

#if GLK_DO_ADAP == 1
#  define GLK_MUST_UPDATE_QUEUE_LENGTH(na) unlikely(na & GLK_SAMPLE_LOCK_EVERY) == 0
#  define GLK_MUST_TRY_ADAPT(na)           unlikely(na & GLK_ADAPT_LOCK_EVERY) == 0
#else
#  warning "GLK adaptiveness is DISABLED"
#  define GLK_MUST_UPDATE_QUEUE_LENGTH(na) 0
#  define GLK_MUST_TRY_ADAPT(na)           0
#endif

#define CACHE_LINE_SIZE 64
#define ALIGNED(N)      __attribute__ ((aligned (N)))
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#if GLK_DEBUG_PRINT == 1 && GLS_NO_PRINT != 1
#  define glk_dlog(...) { printf(__VA_ARGS__); fflush(stdout); }
#else
#  define glk_dlog(...) { }//fflush(stdout); }
#endif

/////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////// TICKET
/////////////////////////////////////////////////////////////////////////////////////
#define GLK_TICKET_LOCK_INITIALIZER { .head = 1, .tail = 0 }

typedef struct glk_ticket_lock 
{
  volatile uint32_t head;
  volatile uint32_t tail;
} glk_ticket_lock_t;


/////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////// MCS
/////////////////////////////////////////////////////////////////////////////////////

#define GLK_MCS_NESTED_LOCKS_MAX 16

typedef volatile struct glk_mcs_lock 
{
  volatile uint64_t waiting;
  volatile struct glk_mcs_lock* next;
} glk_mcs_lock_t;

typedef volatile struct glk_mcs_lock_local_s
{
  glk_mcs_lock_t* head[GLK_MCS_NESTED_LOCKS_MAX];
  /* uint8_t head_padding[CACHE_LINE_SIZE - GLK_MCS_NESTED_LOCKS_MAX*sizeof(glk_mcs_lock_t*)]; */
  glk_mcs_lock_t lock[GLK_MCS_NESTED_LOCKS_MAX];
} glk_mcs_lock_local_t;

#define GLK_MCS_LOCK_INITIALIZER { .waiting = 0, .next = NULL }

extern __thread glk_mcs_lock_local_t __glk_mcs_local;
#define GLK_MCS_LOCAL_DATA					\
  __thread glk_mcs_lock_local_t __glk_mcs_local = { .head = { NULL, NULL, NULL, NULL} }

extern int glk_mcs_lock_trylock(glk_mcs_lock_t* lock);
extern int glk_mcs_lock_queue_length(glk_mcs_lock_t* lock);
extern int glk_mcs_lock_unlock(glk_mcs_lock_t* lock);
extern int glk_mcs_lock_init(glk_mcs_lock_t* lock, pthread_mutexattr_t* a);
extern int glk_mcs_lock_destroy(glk_mcs_lock_t* the_lock);
extern glk_mcs_lock_t* glk_mcs_get_local_lock(glk_mcs_lock_t* head);
extern glk_mcs_lock_t* glk_mcs_get_local_unlock(glk_mcs_lock_t* head);
extern glk_mcs_lock_t* glk_mcs_get_local_nochange(glk_mcs_lock_t* head);


/////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////// MUTEX
/////////////////////////////////////////////////////////////////////////////////////

#define GLK_MUTEX_SPIN_TRIES_LOCK       8192 /* spinning retries before futex */
#define GLK_MUTEX_INITIALIZER	\
  {						\
   .l.u = 0,					\
      }

typedef struct glk_mutex_lock
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
} glk_mutex_lock_t;

#define GLK_MUTEX_FOR_N_CYCLES(n, do)		\
  {								\
    uint64_t ___s = glk_mutex_getticks();		\
    while (1)							\
      {								\
	do;							\
	uint64_t ___e = glk_mutex_getticks();	\
	if ((___e - ___s) > n)					\
	  {							\
	    break;						\
	  }							\
      }								\
  }


static inline uint64_t glk_mutex_getticks(void)
{
  unsigned hi, lo;
  __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
  return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

/////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////// ADAPTIVE
/////////////////////////////////////////////////////////////////////////////////////

typedef volatile int glk_type_t;

typedef struct glk
{
  volatile glk_type_t lock_type;
#if PADDING == 1
  volatile uint8_t padding0[CACHE_LINE_SIZE - sizeof(glk_type_t)];
#endif
  glk_ticket_lock_t ticket_lock;
  glk_mcs_lock_t mcs_lock;
  glk_mutex_lock_t mutex_lock;
  volatile uint32_t num_acquired;
  volatile uint32_t queue_total;
#if PADDING == 1
  volatile uint8_t padding1[CACHE_LINE_SIZE
  			    - sizeof(glk_ticket_lock_t)
  			    - sizeof(glk_mcs_lock_t)
  			    - sizeof(glk_mutex_lock_t)
  			    - 2 * sizeof(uint32_t)];
#endif
} glk_t;


typedef struct glk_cond
{
  uint32_t ticket;
  volatile uint32_t head;
  glk_t * l;
#if PADDING == 1
    uint8_t padding[CACHE_LINE_SIZE - 2*sizeof(uint32_t) - sizeof(glk_t *)];
#endif
} glk_cond_t;

#define GLK_COND_INITIALIZER { 0, 0, NULL }


/* returns 0 on success */
extern int glk_trylock(glk_t *lock);
extern int gls_is_multiprogramming();

extern int glk_lock(glk_t *lock);
extern int glk_unlock(glk_t *lock);

extern int glk_is_free(glk_t *lock);

extern int glk_init(glk_t *lock, const pthread_mutexattr_t* a);
extern int glk_destroy(glk_t *lock);


extern int glk_cond_wait(glk_cond_t* c, glk_t* m);
extern int glk_cond_timedwait(glk_cond_t* c, glk_t* m, const struct timespec* ts);
extern int glk_cond_init(glk_cond_t* c, const pthread_condattr_t* a);
extern int glk_cond_destroy(glk_cond_t* c);
extern int glk_cond_signal(glk_cond_t* c);
extern int glk_cond_broadcast(glk_cond_t* c);

static inline int
glk_timedlock(glk_t* l, const struct timespec* ts)
{
  fprintf(stderr, "** warning -- pthread_mutex_timedlock not implemented\n");
  return 0;
}


/////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////// Help stuff
/////////////////////////////////////////////////////////////////////////////////////

typedef struct periodic_info_s
{
  unsigned long period_us;
  uint32_t num_zeroes_required;
  uint32_t num_zeroes_encountered;
} periodic_data_t;


static inline void* 
swap_ptr(volatile void* ptr, void *x) 
{
  asm volatile("xchgq %0,%1"
	       :"=r" ((unsigned long long) x)
	       :"m" (*(volatile long long *)ptr), "0" ((unsigned long long) x)
	       :"memory");

  return x;
}

#endif
