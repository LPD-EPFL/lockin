/*
 * File: glk.c
 * Authors: Jelena Antic <jelena.antic@epfl.ch>
 *          Georgios Chatzopoulos <georgios.chatzopoulos@epfl.ch>
 *          Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description:
 *     The Generic Lock (GLK) algorithm implementation.
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Jelena Antic, Georgios Chatzopoulos, Vasileios Trigonakis
 *	      	     Distributed Programming Lab (LPD), EPFL
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

#include "glk.h"
#include <sys/time.h>
#include <string.h>

// Background task for multiprogramming detection
static volatile ALIGNED(CACHE_LINE_SIZE) int multiprogramming = 0;

#if GLK_THREAD_CACHE == 1
struct ALIGNED(CACHE_LINE_SIZE) glk_thread_cache
{
  glk_t* lock;
  uint32_t lock_type;
};
static __thread struct glk_thread_cache __thread_cache = { NULL, 0 };
#endif

static inline void
glk_thread_cache_set(glk_t* lock, uint32_t type)
{
#if GLK_THREAD_CACHE == 1
  __thread_cache.lock = lock;
  __thread_cache.lock_type = type;
#endif
}

static inline  int
glk_thread_cache_get_type(glk_t* lock)
{
#if GLK_THREAD_CACHE == 1
  if (likely(__thread_cache.lock == lock))
    {
      return __thread_cache.lock_type;
    }
  else
    {
      return lock->lock_type;
    }
#else
  return lock->lock_type;
#endif
}

static int glk_mcs_lock_lock(glk_t* lock);
static inline int gls_adaptinve_mcs_lock_queue_length(glk_mcs_lock_t* lock);
static int glk_ticket_lock_lock(glk_t* gl);
static inline int glk_ticket_lock_unlock(glk_ticket_lock_t* lock);
static inline int glk_ticket_lock_trylock(glk_ticket_lock_t* lock);
static inline int glk_ticket_lock_init(glk_ticket_lock_t* the_lock, const pthread_mutexattr_t* a);


static inline int glk_mutex_lock(glk_mutex_lock_t* lock);
static inline int glk_mutex_unlock(glk_mutex_lock_t* m);
static inline int glk_mutex_lock_trylock(glk_mutex_lock_t* m);
static inline int glk_mutex_init(glk_mutex_lock_t* m);


void*
glk_mp_check(void *arg)
{  
  FILE *f;
  float lavg[3];
  int nr_running, nr_tot, nr_what;
  int nr_hw_ctx = sysconf(_SC_NPROCESSORS_ONLN);
  const char* lafile = "/proc/loadavg";
  periodic_data_t *data = (periodic_data_t *) arg;

  unsigned int sec = data->period_us / 1000000;
  unsigned long ns = (data->period_us - (sec * 1000000)) * 1000;
  struct timespec timeout;
  timeout.tv_sec = sec;
  timeout.tv_nsec = ns;

  /* unpin the thread */
  int cpu = -1;
  cpu_set_t mask;
  CPU_ZERO(&mask);
  CPU_SET(cpu, &mask);
  if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) != 0)
    {
      fflush(stdout);
    }

  struct timespec time_start, time_stop;
  size_t ms_start = 0, n_sec = 0, n_adap = 0, n_mp_run = 0;
  int time_start_is_set = 0;

  size_t mp_fixed_times = 0;

  while (1)
    {
      n_mp_run++;
      if ((f = fopen(lafile, "r")) == NULL)
	{
	  fprintf(stderr, "Error opening %s file\n", lafile);
	  return 0;
	}
      int n = fscanf(f, "%f %f %f %d/%d %d", &lavg[0], &lavg[1], &lavg[2], &nr_running, &nr_tot, &nr_what);
      if (n != 6)
	{
	  fprintf(stderr, "Error reading %s\n", lafile);
	  continue;
	}

      nr_running--;
      if (nr_running > nr_hw_ctx + GLK_MP_CHECK_THRESHOLD_HIGH)
	{
	  data->num_zeroes_encountered = 0;
	  if (!multiprogramming)
	    {
	      if ((data->num_zeroes_required <<= GLK_MP_NUM_ZERO_SHIFT) > GLK_MP_NUM_ZERO_MAX)
		{
		  data->num_zeroes_required = GLK_MP_NUM_ZERO_MAX;
		}
	      multiprogramming = 1;
	      n_adap++;
	      time_t tm = time(NULL);
	      const char* tms = ctime(&tm);
	      __attribute__((unused)) int len = strlen(tms);
	      glk_dlog("[.BACKGRND] (%.*s) switching TO multiprogramming: (%-3d)\n",
	      		 len - 1, tms, nr_running - 1);
	      timeout.tv_nsec <<= GLK_MP_SLEEP_SHIFT;
	    }
	}
      else if (nr_running < (nr_hw_ctx - GLK_MP_CHECK_THRESHOLD_LOW))
	{
	  if (multiprogramming)
	    {
	      data->num_zeroes_encountered++;
	      if (data->num_zeroes_encountered >= data->num_zeroes_required)
		{
		  multiprogramming = 0;
		  n_adap++;
		  time_t tm = time(NULL);
		  const char* tms = ctime(&tm);
		  __attribute__((unused)) int len = strlen(tms);
		  glk_dlog("[.BACKGRND] (%.*s) switching TO spinning        : (%-3d) (limit %d)\n",
		  	     len - 1, tms, nr_running - 1, data->num_zeroes_required);
		  timeout.tv_nsec >>= GLK_MP_SLEEP_SHIFT;
		}
	    }
	  else
	    {
	      if (--data->num_zeroes_required < GLK_MP_NUM_ZERO_REQ)
		{
		  data->num_zeroes_required = GLK_MP_NUM_ZERO_REQ;
		}
	    }
	}

      fclose(f);
      

      if (time_start_is_set == 0)
      	{
	  clock_gettime(CLOCK_REALTIME, &time_start);
	  time_start_is_set = 1;
	  ms_start = (time_start.tv_sec * 1000) + (time_start.tv_nsec / 1e6);
	}

      nanosleep(&timeout, NULL);

      clock_gettime(CLOCK_REALTIME, &time_stop);
      size_t ms_stop = (time_stop.tv_sec * 1000) + (time_stop.tv_nsec / 1e6);
      size_t ms_diff = ms_stop - ms_start;
      if (ms_diff >= 1000)
	{
	  n_sec++;
	  time_start_is_set = 0;
	  double sec = ms_diff / 1000.0;
	  size_t adap_per_sec = n_adap / sec;
	  glk_dlog("[.BACKGRND] [%-4zu seconds / %-4zu ms]: adaps/s = %-3zu | mp_checks/s = %3.0f\n",
		     n_sec, ms_diff, adap_per_sec, n_mp_run / sec);
	  n_adap = 0;
	  n_mp_run = 0;
	  if (adap_per_sec > GLK_MP_MAX_ADAP_PER_SEC)
	    {
	      multiprogramming = 1;
	      size_t sleep_shift = mp_fixed_times++;
	      if (sleep_shift > GLK_MP_FIXED_SLEEP_MAX_LOG)
		{
		  sleep_shift = GLK_MP_FIXED_SLEEP_MAX_LOG; 
		}
	      size_t mp_fixed_sleep_s = 1LL << sleep_shift;
	      struct timespec mp_fixed_sleep = { .tv_sec = mp_fixed_sleep_s, .tv_nsec = 0};
	      glk_dlog("[.BACKGRND] fixing mp for %zu seconds\n", mp_fixed_sleep_s);
	      n_sec += mp_fixed_sleep_s;
	      nanosleep(&mp_fixed_sleep, NULL);
	    }
	  else if (adap_per_sec <= GLK_MP_LOW_ADAP_PER_SEC)
	    {
	      mp_fixed_times >>= 1;
	    }
	}
    }
  return NULL;
}

static volatile int adaptive_lock_global_initialized = 0;
static volatile int adaptive_lock_global_initializing = 0;

inline int
gls_is_multiprogramming()
{
  return multiprogramming;
}

void adaptive_lock_global_init()
{
  if (__sync_val_compare_and_swap(&adaptive_lock_global_initializing, 0, 1) == 1)
    {
      return;
    }

  assert(adaptive_lock_global_initialized == 0);
#if GLK_DO_ADAP == 1
  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  periodic_data_t* data = (periodic_data_t*) malloc(sizeof(periodic_data_t));
  assert(data != NULL);
  data->num_zeroes_required = GLK_MP_NUM_ZERO_REQ;
  data->num_zeroes_encountered = 0;

  data->period_us = GLK_MP_CHECK_PERIOD_US;
  if (pthread_create(&thread, &attr, glk_mp_check, (void*) data) != 0)
    {
      fprintf(stderr, "Error creating thread\n");
      exit(1);
    }
  pthread_attr_destroy(&attr);
#endif

  adaptive_lock_global_initialized = 1;
}


inline void
unlock_lock(glk_t* lock, const int type)
{
  switch(type)
    {
    case TICKET_LOCK:
      glk_ticket_lock_unlock(&lock->ticket_lock);
      break;
    case MCS_LOCK:
      glk_mcs_lock_unlock(&lock->mcs_lock);
      break;
    case MUTEX_LOCK:
      glk_mutex_unlock(&lock->mutex_lock);
      break;
    }
}

inline int
glk_unlock(glk_t* lock) 
{
  const int current_lock_type = glk_thread_cache_get_type(lock);
  switch(current_lock_type)
    {
    case TICKET_LOCK:
      return glk_ticket_lock_unlock(&lock->ticket_lock);
    case MCS_LOCK:
      return glk_mcs_lock_unlock(&lock->mcs_lock);
    case MUTEX_LOCK:
      return glk_mutex_unlock(&lock->mutex_lock);
    }
  return 0;
}

/* returns 0 on success */
inline int
glk_trylock(glk_t* lock)
{
  do
    {
      const int current_lock_type = lock->lock_type;
      switch(current_lock_type)
	{
	case TICKET_LOCK:
	  if (glk_ticket_lock_trylock(&lock->ticket_lock))
	    {
	      return 1;
	    }
	  break;
	case MCS_LOCK:
	  if (glk_mcs_lock_trylock(&lock->mcs_lock))
	    {
	      return 1;
	    }
	  break;
	case MUTEX_LOCK:
	  if (glk_mutex_lock_trylock(&lock->mutex_lock))
	    {
	      return 1;
	    }
	  break;
	}

      if (unlikely(lock->lock_type == current_lock_type))
	{
	  break;
	}
      else
	{
	  unlock_lock(lock, current_lock_type);
	}
    }
  while (1);
  return 0;
}



static inline void
glk_ticket_adap(glk_t* lock, const uint32_t ticket)
{
  if (GLK_MUST_TRY_ADAPT(ticket))
    {
      if (unlikely(adaptive_lock_global_initialized == 0))
	{
	  adaptive_lock_global_init();
	}

#if GLK_MP_LOW_CONTENTION_TICKET == 1
	  const int do_it = 0;
#else
	  const int do_it = 1;
#endif

      if (do_it && unlikely(multiprogramming))
	{
	  glk_dlog("[%p] %-7s ---> %-7s\n", lock, "TICKET", "MUTEX");
	  lock->lock_type = MUTEX_LOCK;
	}
      else
	{
	  const uint32_t queue_total_local = lock->queue_total;
	  const double ratio = ((double) queue_total_local) / GLK_SAMPLE_NUM;
	  if (unlikely(ratio >= GLK_CONTENTION_RATIO_HIGH))
	    {
#if GLK_MP_LOW_CONTENTION_TICKET == 1
	      /* move away from tas iff there is multiprogramming */
	      if (unlikely(multiprogramming))
		{
		  glk_dlog("[%p] %-7s ---> %-7s : queue_total %-4u - samples %-5u = %f\n",
			     lock, "TICKET", "MUTEX", lock->queue_total, GLK_SAMPLE_NUM, ratio);
		  lock->lock_type = MUTEX_LOCK;
		  return;
		}
#endif

	      glk_dlog("[%p] %-7s ---> %-7s : queue_total %-4u - samples %-5u = %f\n",
				lock, "TICKET", "MCS", lock->queue_total, GLK_SAMPLE_NUM, ratio);
	      lock->queue_total = queue_total_local >> GLK_MEASURE_SHIFT;
	      lock->num_acquired = GLK_NUM_ACQ_INIT;
	      lock->lock_type = MCS_LOCK;
	    }
	  else 
	    {
	      lock->queue_total = 0;
	      lock->num_acquired = GLK_NUM_ACQ_INIT;
	    }
	}
    }
}

static inline void
glk_mcs_adap(glk_t* lock, const int num_acq)
{
  if (GLK_MUST_UPDATE_QUEUE_LENGTH(num_acq))
    {

      const int len = gls_adaptinve_mcs_lock_queue_length(&lock->mcs_lock);
      const int queue_total_local = __sync_add_and_fetch(&lock->queue_total, len);

      if (GLK_MUST_TRY_ADAPT(num_acq))
	{
	  if (unlikely(multiprogramming))
	    {
	      glk_dlog("[%p] %-7s ---> %-7s\n", lock, "MCS", "MUTEX");
	      lock->lock_type = MUTEX_LOCK;
	    }
	  else
	    {
	      const double ratio = ((double) queue_total_local) / GLK_SAMPLE_NUM;
	      if (unlikely(ratio < GLK_CONTENTION_RATIO_LOW))
		{
		  glk_dlog("[%p] %-7s ---> %-7s : queue_total %-4u - samples %-5u = %f\n",
				    lock, "MCS", "TICKET", lock->queue_total, GLK_SAMPLE_NUM, ratio);
		  lock->queue_total = 0;
		  lock->num_acquired = GLK_NUM_ACQ_INIT;
		  lock->lock_type = TICKET_LOCK;
		}
	      else 
		{
		  lock->queue_total = queue_total_local >> GLK_MEASURE_SHIFT;
		  lock->num_acquired = GLK_NUM_ACQ_INIT;
		}
	    }
	}
    }
}


static inline void
glk_mutex_adap(glk_t* lock)
{
#if GLK_DO_ADAP == 1
  if (unlikely(!multiprogramming))
    {
      if (likely(lock->lock_type == MUTEX_LOCK))
	{
	  lock->queue_total = 0;
	  lock->num_acquired = 0;
	  lock->lock_type = GLK_MP_TO_LOCK;
	  glk_dlog("[%p] %-7s ---> %-7s\n", lock, "MUTEX",
		     (GLK_MP_TO_LOCK == MCS_LOCK) ? "MCS" : "TICKET");
	}
    }
#endif
}

inline int
glk_lock(glk_t* lock) 
{
  int ret = 0;
  do
    {
      const int current_lock_type = lock->lock_type;
      glk_thread_cache_set(lock, current_lock_type);
      switch(current_lock_type)
	{
	case TICKET_LOCK:
	  {
	    const uint32_t ticket  = glk_ticket_lock_lock(lock);
	    glk_ticket_adap(lock, ticket);
	  }
	  break;
	case MCS_LOCK:
	  {
	    int num_acq = glk_mcs_lock_lock(lock);
	    glk_mcs_adap(lock, num_acq);
	  }
	  break;
	case MUTEX_LOCK:
	  {
	    glk_mutex_lock(&lock->mutex_lock);
	    glk_mutex_adap(lock);
	  }
	  break;
	}

      if (likely(lock->lock_type == current_lock_type))
	{
	  break;
	}
      else
	{
	  unlock_lock(lock, current_lock_type);
	}
    }
  while (1);

  return ret;
}

int glk_destroy(glk_t *lock) {
  return 0;
}

int
glk_init(glk_t *lock, const pthread_mutexattr_t* a)
{
  if (unlikely(adaptive_lock_global_initialized == 0))
    {
      adaptive_lock_global_init();
    }

  if (gls_is_multiprogramming())
    {
      lock->lock_type = GLK_INIT_LOCK_TYPE_MP;
    }
  else
    {
      lock->lock_type = GLK_INIT_LOCK_TYPE;
    }

  glk_ticket_lock_init(&lock->ticket_lock, a);
  glk_mcs_lock_init(&lock->mcs_lock, (pthread_mutexattr_t*) a);
  glk_mutex_init(&lock->mutex_lock);
  lock->num_acquired = 0;
  lock->queue_total = 0;

  /* asm volatile ("mfence"); */
  return 0;
}

/* ******************************************************************************** */
/* lock implementations */
/* ******************************************************************************** */

/* **************************************** */
/* MCS */
/* **************************************** */

GLK_MCS_LOCAL_DATA;

int 
glk_mcs_lock_init(glk_mcs_lock_t* lock, pthread_mutexattr_t* a)
{
  lock->next = NULL;
  return 0;
}

inline glk_mcs_lock_t*
glk_mcs_get_local_lock(glk_mcs_lock_t* head)
{
  int i;
  glk_mcs_lock_local_t *local = (glk_mcs_lock_local_t*) &__glk_mcs_local;
  for (i = 0; i <= GLK_MCS_NESTED_LOCKS_MAX; i++)
    {
      if (local->head[i] == NULL)
	{
	  local->head[i] = head;
	  return &local->lock[i];
	}

    }
  return NULL;
}

inline glk_mcs_lock_t*
glk_mcs_get_local_unlock(glk_mcs_lock_t* head)
{
  int i;
  glk_mcs_lock_local_t *local = (glk_mcs_lock_local_t*) &__glk_mcs_local;
  for (i = 0; i <= GLK_MCS_NESTED_LOCKS_MAX; i++)
    {
      if (local->head[i] == head)
	{
	  local->head[i] = NULL;
	  return &local->lock[i];
	}
    }
  return NULL;
}


inline glk_mcs_lock_t*
glk_mcs_get_local_nochange(glk_mcs_lock_t* head)
{
  int i;
  glk_mcs_lock_local_t *local = (glk_mcs_lock_local_t*) &__glk_mcs_local;
  for (i = 0; i <= GLK_MCS_NESTED_LOCKS_MAX; i++)
    {
      if (local->head[i] == head)
	{
	  return &local->lock[i];
	}
    }
  return NULL;
}

static inline int
gls_adaptinve_mcs_lock_queue_length(glk_mcs_lock_t* lock)
{
  int res = 1;
  glk_mcs_lock_t *curr = glk_mcs_get_local_nochange(lock)->next;
  while (curr != NULL)
    {
      curr = curr->next;
      res++;
    }
  return res;
}

static int
glk_mcs_lock_lock(glk_t* gl) 
{
  glk_mcs_lock_t* lock = &gl->mcs_lock;
  volatile glk_mcs_lock_t* local = glk_mcs_get_local_lock(lock);
  local->next = NULL;
  
  glk_mcs_lock_t* pred = swap_ptr((void*) &lock->next, (void*) local);
#if GLK_DO_ADAP == 1
  const int num_acq = __sync_add_and_fetch(&gl->num_acquired, 1);
#else
  const int num_acq = 0;
#endif
  /* gl->num_acquired++; */

  if (pred == NULL)  		/* lock was free */
    {
      return num_acq;
    }
  local->waiting = 1; // word on which to spin
  pred->next = local; // make pred point to me 

  size_t n_spins = 0;
  while (local->waiting != 0) 
    {
      if (unlikely((n_spins++ == 1024) && gls_is_multiprogramming()))
	{
	  n_spins = 0;
	  pthread_yield();
	}
      PAUSE_IN();
    }
  return num_acq;
}

inline int
glk_mcs_lock_trylock(glk_mcs_lock_t* lock) 
{
  if (lock->next != NULL)
    {
      return 1;
    }

  volatile glk_mcs_lock_t* local = glk_mcs_get_local_lock(lock);
  local->next = NULL;
  if (__sync_val_compare_and_swap(&lock->next, NULL, local) != NULL)
    {
      glk_mcs_get_local_unlock(lock);
      return 1;
    }
  return 0;
}

inline int
glk_mcs_lock_unlock(glk_mcs_lock_t* lock) 
{
  volatile glk_mcs_lock_t* local = glk_mcs_get_local_unlock(lock);
  volatile glk_mcs_lock_t* succ;

  if (!(succ = local->next)) /* I seem to have no succ. */
    { 
      /* try to fix global pointer */
      if (__sync_val_compare_and_swap(&lock->next, local, NULL) == local) 
	{
	  return 0;
	}
      do 
	{
	  succ = local->next;
	  PAUSE_IN();
	} 
      while (!succ); // wait for successor
    }
  succ->waiting = 0;
  return 0;

}

/* **************************************** */
/* ticket */
/* **************************************** */

static inline int
glk_ticket_lock_lock(glk_t* gl)
{
  glk_ticket_lock_t* lock = &gl->ticket_lock;
  const uint32_t ticket = __sync_add_and_fetch(&(lock->tail), 1);
  const int distance = ticket - lock->head;
  if (likely(distance == 0))
    {
      return ticket;
    }

  if (GLK_MUST_UPDATE_QUEUE_LENGTH(ticket))
    {
      __sync_add_and_fetch(&gl->queue_total, distance);
    }
  
  size_t n_spins = 0;
  do
    {
      const int distance = ticket - lock->head;
      if (unlikely(distance == 0))
	{
	  break;
	}

      if (unlikely((n_spins++ == 1024) && gls_is_multiprogramming()))
	{
	  n_spins = 0;
	  pthread_yield();
	}
      PAUSE_IN();
    }
  while (1);
  return ticket;
}

static inline int
glk_ticket_lock_unlock(glk_ticket_lock_t* lock) 
{
  asm volatile("" ::: "memory");
#if defined(MEMCACHED)
  if (__builtin_expect((lock->tail >= lock->head), 1)) 
    {
#endif
      lock->head++;
#if defined(MEMCACHED)
    }
#endif
  return 0;
}

static inline int
glk_ticket_lock_trylock(glk_ticket_lock_t* lock) 
{
  uint32_t to = lock->tail;
  if (lock->head - to == 1)
    {
      return (__sync_val_compare_and_swap(&lock->tail, to, to + 1) != to);
    }

  return 1;
}

static inline int
glk_ticket_lock_init(glk_ticket_lock_t* the_lock, const pthread_mutexattr_t* a) 
{
  the_lock->head=1;
  the_lock->tail=0;
  return 0;
}


int
glk_is_free(glk_t* lock)
{
  do
    {
      const glk_type_t current_lock_type = lock->lock_type;
      switch(current_lock_type)
	{
	case TICKET_LOCK:
	  if (lock->ticket_lock.head - lock->ticket_lock.tail == 1)
	    {
	      return 1;
	    }
	  break;
	case MCS_LOCK:
	  if (lock->mcs_lock.next == NULL)
	    {
	      return 1;
	    }
	  break;
	case MUTEX_LOCK:
	  if (lock->mutex_lock.l.b.locked == 0)
	    {
	      return 1;
	    }
	  break;
	}

      if (likely(lock->lock_type == current_lock_type))
	{
	  break;
	}
    }
  while (1);
  return 0;
}
/* **************************************** */
/* mutex */
/* **************************************** */

static inline int
sys_futex_glk_mutex(void* addr1, int op, int val1, struct timespec* timeout, void* addr2, int val3)
{
  return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}

//Swap uint32_t
static inline uint32_t
glk_mutex_swap_uint32(volatile uint32_t* target,  uint32_t x)
{
  asm volatile("xchgl %0,%1"
	       :"=r" ((uint32_t) x)
	       :"m" (*(volatile uint32_t *)target), "0" ((uint32_t) x)
	       :"memory");

  return x;
}

//Swap uint8_t
static inline uint8_t
glk_mutex_swap_uint8(volatile uint8_t* target,  uint8_t x)
{
  asm volatile("xchgb %0,%1"
	       :"=r" ((uint8_t) x)
	       :"m" (*(volatile uint8_t *)target), "0" ((uint8_t) x)
	       :"memory");

  return x;
}

#define cmpxchg(a, b, c) __sync_val_compare_and_swap(a, b, c)
#define xchg_32(a, b)    glk_mutex_swap_uint32((uint32_t*) a, b)
#define xchg_8(a, b)     glk_mutex_swap_uint8((uint8_t*) a, b)

static inline int
glk_mutex_lock(glk_mutex_lock_t* m)
{
  if (!xchg_8(&m->l.b.locked, 1))
    {
      return 0;
    }

  const unsigned int time_spin = GLK_MUTEX_SPIN_TRIES_LOCK;
  GLK_MUTEX_FOR_N_CYCLES(time_spin,
			     if (!xchg_8(&m->l.b.locked, 1))
			       {
				 return 0;
			       }
			     PAUSE_IN();
			     );


  /* Have to sleep */
  while (xchg_32(&m->l.u, 257) & 1)
    {
      sys_futex_glk_mutex(m, FUTEX_WAIT_PRIVATE, 257, NULL, NULL, 0);
    }
  return 0;

}

static inline int
glk_mutex_unlock(glk_mutex_lock_t* m)
{
  /* Locked and not contended */
  if ((m->l.u == 1) && (cmpxchg(&m->l.u, 1, 0) == 1))
    {
      return 0;
    }

  /* Unlock */
  m->l.b.locked = 0;
  asm volatile ("mfence");

  if (m->l.b.locked)
    {
      return 0;
    }

  /* We need to wake someone up */
  m->l.b.contended = 0;

  sys_futex_glk_mutex(m, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
  return 0;
}

static inline int
glk_mutex_lock_trylock(glk_mutex_lock_t* m)
{
  unsigned c = xchg_8(&m->l.b.locked, 1);
  if (!c)
    {
      return 0;
    }
  return EBUSY;
}

static inline int
glk_mutex_init(glk_mutex_lock_t* m)
{
  m->l.u = 0;
  return 0;
}


/* **************************************** */
/* conditionals */
/* **************************************** */


inline int
glk_cond_wait(glk_cond_t* c, glk_t* m)
{
  int head = c->head;

  if (c->l != m)
    {
      if (c->l) return EINVAL;

      /* Atomically set mutex inside cv */
      __attribute__ ((unused)) int dummy = (uintptr_t) __sync_val_compare_and_swap(&c->l, NULL, m);
      if (c->l != m) return EINVAL;
    }

  glk_unlock(m);

  sys_futex_glk_mutex((void*) &c->head, FUTEX_WAIT_PRIVATE, head, NULL, NULL, 0);

  glk_lock(m);

  return 0;
}


inline int
glk_cond_timedwait(glk_cond_t* c, glk_t* m, const struct timespec* ts)
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

  glk_unlock(m);

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

  sys_futex_glk_mutex((void*) &c->head, FUTEX_WAIT_PRIVATE, head, &rt, NULL, 0);

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
 glk_lock(m);

  return ret;
}

inline int
glk_cond_init(glk_cond_t* c, const pthread_condattr_t* a)
{
  (void) a;

  c->l = NULL;

  /* Sequence variable doesn't actually matter, but keep valgrind happy */
  c->head = 0;

  return 0;
}

inline int
glk_cond_destroy(glk_cond_t* c)
{
  /* No need to do anything */
  (void) c;
  return 0;
}

inline int
glk_cond_signal(glk_cond_t* c)
{
  /* We are waking someone up */
  __sync_add_and_fetch(&c->head, 1);

  /* Wake up a thread */
  sys_futex_glk_mutex((void*) &c->head, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);

  return 0;
}

inline int
glk_cond_broadcast(glk_cond_t* c)
{
  glk_t* m = c->l;

  /* No mutex means that there are no waiters */
  if (!m) return 0;

  /* We are waking everyone up */
  __sync_add_and_fetch(&c->head, 1);

  /* Wake one thread, and requeue the rest on the mutex */
  sys_futex_glk_mutex((void*) &c->head, FUTEX_REQUEUE_PRIVATE, INT_MAX, NULL, NULL, 0);

  return 0;
}
