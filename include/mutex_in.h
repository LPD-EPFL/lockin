#ifndef _MUTEX_IN_H_
#define _MUTEX_IN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#ifndef __sparc__
#include <numa.h>
#endif
#include <pthread.h>

#define LOCK_IN_NAME "MUTEX"

/* settings *********************************************************************** */
#if defined(PADDING)
#  undef PADDING
#endif
#  define PADDING        0        /* padd locks/conditionals to cache-line */

#define REPLACE_MUTEX  1	/* ovewrite the pthread_[mutex|cond] functions */
/* ******************************************************************************** */

#define CACHE_LINE_SIZE 64

#if !defined(PAUSE)
#  define PAUSE()			\
  ;
#endif


typedef struct mutex_lock
{
  pthread_mutex_t lock;
#if PADDING == 1
  uint8_t padding[CACHE_LINE_SIZE - sizeof(pthread_mutex_t)];
#endif
} mutex_lock_t;

#define MUTEX_LOCK_INITIALIZER { .lock = PTHREAD_MUTEX_INITIALIZER_CP }

typedef struct mutex_cond 
{
  pthread_cond_t cond;
#if PADDING == 1
  uint8_t padding[CACHE_LINE_SIZE - sizeof(pthread_cond_t)];
#endif
} mutex_cond_t;

#define MUTEX_COND_INITIALIZER { .cond = PTHREAD_COND_INITIALIZER_CP }

#define _cast_mutex(m) (pthread_mutex_t*) (m)
#define _cast_cond(m) (pthread_cond_t*) (m)

static inline int
mutex_lock_trylock(mutex_lock_t* lock) 
{
  return pthread_mutex_trylock(_cast_mutex(lock));
}

static inline int
mutex_lock_lock(mutex_lock_t* lock) 
{
  return pthread_mutex_lock(_cast_mutex(lock));
}

static inline int
mutex_lock_timedlock(mutex_lock_t* m, const struct timespec* ts)
{
  return  pthread_mutex_timedlock(_cast_mutex(m), ts);
}
 
static inline int
mutex_lock_unlock(mutex_lock_t* lock) 
{
  return pthread_mutex_unlock(_cast_mutex(lock));
}


static inline int
mutex_lock_init(mutex_lock_t* lock, const pthread_mutexattr_t* a) 
{
  return pthread_mutex_init(_cast_mutex(lock), a);
}

static inline int
mutex_lock_destroy(mutex_lock_t* lock) 
{
  return pthread_mutex_destroy(_cast_mutex(lock));
}

static inline int
mutex_cond_wait(mutex_cond_t* c, mutex_lock_t* m)
{
  return pthread_cond_wait(_cast_cond(c), _cast_mutex(m));
}

static inline int
mutex_cond_timedwait(mutex_cond_t* c, mutex_lock_t* m, const struct timespec* ts)
{
  return  pthread_cond_timedwait(_cast_cond(c), _cast_mutex(m), ts);
}

static inline int
mutex_cond_init(mutex_cond_t* c, const pthread_condattr_t* a)
{
  return pthread_cond_init(_cast_cond(c), a);
}

static inline int 
mutex_cond_destroy(mutex_cond_t* c)
{
  return pthread_cond_destroy(_cast_cond(c));
}

static inline int
mutex_cond_signal(mutex_cond_t* c)
{
  return pthread_cond_signal(_cast_cond(c));
}

static inline int
mutex_cond_broadcast(mutex_cond_t* c)
{
  return pthread_cond_broadcast(_cast_cond(c));
}

#if REPLACE_MUTEX == 1
#  define pthread_mutex_init    mutex_lock_init
#  define pthread_mutex_destroy mutex_lock_destroy
#  define pthread_mutex_lock    mutex_lock_lock
#  define pthread_mutex_timedlock mutex_lock_timedlock
#  define pthread_mutex_unlock  mutex_lock_unlock
#  define pthread_mutex_trylock mutex_lock_trylock
#  define pthread_mutex_t       mutex_lock_t
#  define PTHREAD_MUTEX_INITIALIZER_CP { { 0, 0, 0, 0, 0, 0 } }
#  undef  PTHREAD_MUTEX_INITIALIZER
#  define PTHREAD_MUTEX_INITIALIZER MUTEX_LOCK_INITIALIZER

#  define pthread_cond_init     mutex_cond_init
#  define pthread_cond_destroy  mutex_cond_destroy
#  define pthread_cond_signal   mutex_cond_signal
#  define pthread_cond_broadcast mutex_cond_broadcast
#  define pthread_cond_wait     mutex_cond_wait
#  define pthread_cond_timedwait mutex_cond_timedwait
#  define pthread_cond_t        mutex_cond_t
#  define PTHREAD_COND_INITIALIZER_CP { { 0, 0, 0, 0, 0, (void *) 0, 0, 0 } }
#  undef  PTHREAD_COND_INITIALIZER
#  define PTHREAD_COND_INITIALIZER MUTEX_COND_INITIALIZER
#endif

#ifdef __cplusplus
}
#endif

#endif


