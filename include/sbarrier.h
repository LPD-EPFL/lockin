/*   
 *   File: barrier.h
 *   Author: Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *   Description: barrier structures
 *
 */

#ifndef BARRIER_H
#define	BARRIER_H

//#include "common.h"
#include "atomic_ops.h"
#ifdef __sparc__
#  include <sys/types.h>
#  include <sys/processor.h>
#  include <sys/procset.h>
#endif /* __sparc */

#define NUM_BARRIERS 16
#define BARRIER_MEM_FILE "/barrier_mem"

#ifndef ALIGNED
#  if __GNUC__ && !SCC
#    define ALIGNED(N) __attribute__ ((aligned (N)))
#  else
#    define ALIGNED(N)
#  endif
#endif

#define BARRIER_PARTICIPANT_MAX 105

/*barrier type*/
typedef ALIGNED(64) struct barrier
{
  size_t participant_n;
  volatile size_t crossed_n;
  volatile size_t sense_global;
  volatile uint8_t sense_local[BARRIER_PARTICIPANT_MAX];
} barrier_t;


void barrier_init(barrier_t* b, int nt);
void barrier_wait(barrier_t* b, int id);
#define barrier_cross(b) barrier_wait(b, d->id)
void barriers_term();

// sleep-based barriers

typedef struct barrier_sleep 
{
  pthread_cond_t complete;
  pthread_mutex_t mutex;
  int count;
  int crossing;
} barrier_sleep_t;

void barrier_sleep_init(barrier_sleep_t *b, int n);
void barrier_sleep_cross(barrier_sleep_t *b);

#define EXEC_IN_DEC_ID_ORDER(id, nthr)		\
  { int __i;					\
  for (__i = nthr - 1; __i >= 0; __i--)		\
    {						\
  if (id == __i)				\
    {

#define EXEC_IN_DEC_ID_ORDER_END(barrier)	\
  }						\
    barrier_sleep_cross(barrier);		\
    }}


#define EXEC_IN_ID_ORDER(id, nthr)		\
  { int __i;					\
  for (__i = 0; __i < nthr ; __i++)		\
    {						\
  if (id == __i)				\
    {

#define EXEC_IN_ID_ORDER_END(barrier)		\
  }						\
    barrier_sleep_cross(barrier);		\
    }}



#endif	/* BARRIER_H */
