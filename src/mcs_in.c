/*
 * File: mcs_in.c
 * Author: Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description: 
 *      MCS lock implementation
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

#include "mcs_in.h"

#if defined(PAUSE_IN)
#  if PAUSE_IN == 0
#    undef PAUSE_IN
#    define PAUSE_IN()
#  elif PAUSE_IN == 1
#    undef PAUSE_IN
#    define PAUSE_IN() asm volatile("nop");
#  elif PAUSE_IN == 2
#    undef PAUSE_IN
#    define PAUSE_IN() asm volatile("pause");
#  elif PAUSE_IN == 3
#    undef PAUSE_IN
#    define PAUSE_IN() asm volatile("mfence");
#  elif PAUSE_IN == 4
#    undef PAUSE_IN
#    define PAUSE_IN() asm volatile("nop"); asm volatile("nop"); asm volatile("nop"); 
#  else
#    undef PAUSE_IN
#    define PAUSE_IN() 
#    warning unknown code for pause_in mcs
#  endif
#endif

static __thread int __mcs_id = -1;
static volatile int __mcs_ids = 0;

static inline volatile mcs_lock_t*
mcs_get_local(mcs_lock_t* lock)
{
  if (__builtin_expect(__mcs_id < 0, 0))
    {
      __mcs_id = __sync_fetch_and_add(&__mcs_ids, 1);
      assert(__mcs_id < MCS_MAX_THR);
    }

  if (__builtin_expect(lock->local[__mcs_id] == NULL, 0))
    {
      lock->local[__mcs_id] = (mcs_lock_t*) malloc(sizeof(mcs_lock_local_t));
      assert(lock->local[__mcs_id] != NULL);
    }

  return lock->local[__mcs_id];
}

inline int
mcs_lock_trylock(mcs_lock_t* lock) 
{
  if (lock->next != NULL)
    {
      return 1;
    }

  volatile mcs_lock_t* local = mcs_get_local(lock);
  local->next = NULL;
  return __sync_val_compare_and_swap(&lock->next, NULL, local) != NULL;
}

inline int
mcs_lock_lock(mcs_lock_t* lock) 
{
  volatile mcs_lock_t* local = mcs_get_local(lock);
  local->next = NULL;
  
  mcs_lock_t* pred = swap_ptr((void*) &lock->next, (void*) local);

  if (pred == NULL)  		/* lock was free */
    {
      return 0;
    }
  local->waiting = 1; // word on which to spin
  pred->next = local; // make pred point to me
  while (local->waiting != 0) 
    {
      PAUSE_IN();
    }
  return 0;
}


inline int
mcs_lock_unlock(mcs_lock_t* lock) 
{
  volatile mcs_lock_t* local = lock->local[__mcs_id];
#if defined(MEMCACHED)
  if (__builtin_expect(__mcs_id < 0 || local == NULL, 0))
    {
      return 0;
    }
#endif

  volatile mcs_lock_t* succ;

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

int 
mcs_lock_init(mcs_lock_t* lock, pthread_mutexattr_t* a)
{
  lock->next = NULL;
  int i;
  for (i = 0; i < MCS_MAX_THR; i++)
    {
      lock->local[i] = NULL;
    }
  mcs_get_local(lock);
  asm volatile ("mfence");
  return 0;
}

int 
mcs_lock_destroy(mcs_lock_t* the_lock)
{
  return 0;
}
