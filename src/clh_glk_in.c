/*
 * File: clh_glk_in.c
 * Author: Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description: 
 *      CLH lock implementation for GLS.
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Vasileios Trigonakis
 *	     	       Distributed Programming Lab (LPD), EPFL
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

#include "clh_glk_impl.h"

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

static __thread int __clh_id = -1;
static volatile int __clh_ids = 0;

static inline volatile clh_lock_node_t*
clh_get_local(clh_lock_t* lock)
{
  if (__builtin_expect(__clh_id < 0, 0))
    {
      __clh_id = __sync_fetch_and_add(&__clh_ids, 1);
      assert(__clh_id < CLH_MAX_THR);
    }

  if (__builtin_expect(lock->local[__clh_id] == NULL, 0))
    {
      lock->local[__clh_id] = (clh_lock_node_t*) malloc(sizeof(clh_lock_node_t));
      assert(lock->local[__clh_id] != NULL);
    }

  return lock->local[__clh_id];
}

static inline void
clh_set_local(clh_lock_t* lock, clh_lock_node_t* new)
{
  lock->local[__clh_id] = new;
}

inline int
clh_lock_trylock(clh_lock_t* lock) 
{
  /* volatile clh_lock_node_t* local = clh_get_local(lock); */
  /* volatile clh_lock_node_t* pred = lock->head; */

  /* if (pred == NULL || pred->locked == 0) */
  /*   { */
  /*     local->locked = 1; */

  /*     volatile int i = 1230; while (i--); */
      
  /*     /\* has the ABA problem :-( *\/ */
  /*     if (__sync_val_compare_and_swap(&lock->head, pred, local) == pred) */
  /* 	{ */
  /* 	  if (__builtin_expect(pred == NULL, 0)) */
  /* 	    { */
  /* 	      pred = (clh_lock_node_t*) malloc(sizeof(clh_lock_node_t)); */
  /* 	      assert(pred != NULL); */
  /* 	    } */

  /* 	  local->pred = pred; */
  /* 	  return 0; */
  /* 	} */
  /*   } */

  fprintf(stderr, "clh_lock_trylock() is not yet implemented\n");
  return 1;
}

inline int
clh_lock_lock(clh_lock_t* lock) 
{
  volatile clh_lock_node_t* local = clh_get_local(lock);
  local->locked = 1;
  
  clh_lock_node_t* pred = swap_ptr((void*) &lock->head, (void*) local);
  if (__builtin_expect(pred == NULL, 0))
    {
      pred = (clh_lock_node_t*) malloc(sizeof(clh_lock_node_t));
      assert(pred != NULL);
      local->pred = pred;
      return 0;
    }

  local->pred = pred;

  while (pred->locked == 1)
    {
      PAUSE_IN();
    }
  return 0;
}


inline int
clh_lock_unlock(clh_lock_t* lock) 
{
  volatile clh_lock_node_t* local = clh_get_local(lock);
  clh_lock_node_t* pred = (clh_lock_node_t*) local->pred;

  local->locked = 0;
  clh_set_local(lock, pred);

  return 0;
}

int 
clh_lock_init(clh_lock_t* lock, pthread_mutexattr_t* a)
{
  lock->head = NULL;
  int i;
  for (i = 0; i < CLH_MAX_THR; i++)
    {
      lock->local[i] = NULL;
    }
  clh_get_local(lock);
  asm volatile ("mfence");
  return 0;
}

int 
clh_lock_destroy(clh_lock_t* the_lock)
{
  return 0;
}
