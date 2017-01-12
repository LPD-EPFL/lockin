/*
 * File: mcs_glk_in.c
 * Author: Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description: 
 *      MCS lock implementation for GLS.
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Vasileios Trigonakis
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

#include "mcs_glk_impl.h"

#include "gls_debug.h"

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

MCS_LOCAL_DATA;

inline mcs_lock_t*
mcs_get_local_lock(mcs_lock_t* head)
{
  int i;
  mcs_lock_local_t *local = (mcs_lock_local_t*) &__mcs_local;
  for (i = 0; i <= MCS_NESTED_LOCKS_MAX; i++) {
	  if (local->head[i] == NULL) {
		  local->head[i] = head;
		  return &local->lock[i];
	  }

  }
  GLS_WARNING("nesting level more than %d", "MCS-LOCK", head, MCS_NESTED_LOCKS_MAX);
  assert(0);
  return NULL;
}

inline mcs_lock_t*
mcs_get_local_unlock(mcs_lock_t* head)
{
  int i;
  mcs_lock_local_t *local = (mcs_lock_local_t*) &__mcs_local;
  for (i = 0; i <= MCS_NESTED_LOCKS_MAX; i++) {
	  if (local->head[i] == head) {
		  local->head[i] = NULL;
		  return &local->lock[i];
	  }
  }
  GLS_WARNING("No local node for MCS found", "MCS-UNLOCK", head);
  assert(0);
  return NULL;
}


inline mcs_lock_t*
mcs_get_local_nochange(mcs_lock_t* head)
{
  int i;
  mcs_lock_local_t *local = (mcs_lock_local_t*) &__mcs_local;
  for (i = 0; i <= MCS_NESTED_LOCKS_MAX; i++) {
	  if (local->head[i] == head) {
		  return &local->lock[i];
	  }
  }
  GLS_WARNING("No local node for MCS found", "MCS-UNLOCK", head);
  assert(0);
  return NULL;
}

inline int
mcs_lock_queue_length(mcs_lock_t* lock)
{
	int res = 1;
	mcs_lock_t *curr = mcs_get_local_nochange(lock);
	while (curr) {
		curr = curr->next;
		res++;
	}
	return res;
}


inline int
mcs_lock_trylock(mcs_lock_t* lock) 
{
  if (lock->next != NULL)
    {
      return 1;
    }

  volatile mcs_lock_t* local = mcs_get_local_lock(lock);
  local->next = NULL;
  if (__sync_val_compare_and_swap(&lock->next, NULL, local) != NULL)
  {
	  mcs_get_local_unlock(lock);
	  return 1;
  }
  return 0;
}

inline int
mcs_lock_lock(mcs_lock_t* lock) 
{
  volatile mcs_lock_t* local = mcs_get_local_lock(lock);
  local->next = NULL;
  
  mcs_lock_t* pred = swap_ptr_mcs((void*) &lock->next, (void*) local);

  if (pred == NULL)  		/* lock was free */
    {
      return 0;
    }
  local->waiting = 1; // word on which to spin
  pred->next = local; // make pred point to me

  GLS_DDD(size_t n_spins = 0);
#if MCS_COOP == 1
  size_t spins = 0;
  while (local->waiting != 0) 
    {
      GLS_DD_CHECK(n_spins);
      PAUSE_IN();
      if (spins++ == MCS_MAX_SPINS)
	{
	  spins = 0;
	  pthread_yield();
	}
    }
#else  /* !MCS_COOP */
  while (local->waiting != 0) 
    {
      GLS_DD_CHECK(n_spins);
      PAUSE_IN();
    }
#endif				/* MCS_COOP */
  return 0;
}

inline int
mcs_lock_unlock(mcs_lock_t* lock) 
{
  volatile mcs_lock_t* local = mcs_get_local_unlock(lock);

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
  asm volatile ("mfence");
  return 0;
}

int 
mcs_lock_destroy(mcs_lock_t* the_lock)
{
  return 0;
}
