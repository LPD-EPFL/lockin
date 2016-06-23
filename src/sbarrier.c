/*   
 *   File: barrier.c
 *   Author: Vasileios Trigonakis <vasileios.trigonakis@oracle.com>
 *   Description: implementation of (sense) barriers
 *
 */

#include "sbarrier.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sched.h>
#include <inttypes.h>
#include <malloc.h>
#include <assert.h>

#ifdef __sparc__
#  include <sys/types.h>
#  include <sys/processor.h>
#  include <sys/procset.h>
#endif	/* __sparc__ */

void
barrier_init(barrier_t* b, int tn)
{
  assert(tn <= BARRIER_PARTICIPANT_MAX);
  b->crossed_n = tn;
  b->sense_global = 1;
  int ue;
  for (ue = 0; ue < tn; ue++) 
    {
      b->sense_local[ue] = !b->sense_local;
    }

  b->participant_n = tn;
}

void 
barrier_wait(barrier_t* b, const int id)
{
  COMPILER_BARRIER;

  int sense_local = !b->sense_global;
  if (__sync_fetch_and_sub(&b->crossed_n, 1) == 1)
    {
      b->crossed_n = b->participant_n;
      b->sense_global = sense_local;
    }
  else
    {
      while (sense_local != b->sense_global)
	{
	  asm volatile ("mfence");
	}
    }
  b->sense_local[id] = !sense_local;

  COMPILER_BARRIER;
}

// sleep-based barriers

void
barrier_sleep_init(barrier_sleep_t *b, int n) 
{
  pthread_cond_init(&b->complete, NULL);
  pthread_mutex_init(&b->mutex, NULL);
  b->count = n;
  b->crossing = 0;
}

void 
barrier_sleep_cross(barrier_sleep_t *b) 
{
  pthread_mutex_lock(&b->mutex);
  /* One more thread through */
  b->crossing++;
  /* If not all here, wait */
  if (b->crossing < b->count) 
    {
      pthread_cond_wait(&b->complete, &b->mutex);
    }
  else 
    {
      pthread_cond_broadcast(&b->complete);
      /* Reset for next time */
      b->crossing = 0;
    }
  pthread_mutex_unlock(&b->mutex);
}
