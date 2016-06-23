#ifndef _LOCK_IN_H_
#define _LOCK_IN_H_

#ifdef __cplusplus
extern "C" {
#endif

/* ******************************************************************************** */
/* settings *********************************************************************** */
#if !defined(LOCK_IN)
#  define LOCK_IN MUTEXEE  /* which lock algorithm to use. See below */
#endif

#if !defined(LOCK_IN_RLS_FENCE)
#  define LOCK_IN_RLS_FENCE()			\
  ;
  /* asm volatile ("mfence");	/\* mem fence when releasing the lock *\/ */
#endif				/* (used in tas_in, ttas_in */

#if !defined(USE_FUTEX_COND)
#  define USE_FUTEX_COND   1 /* use futex-based (1) or spin-based futexs */
#endif
#if !defined(PADDING)
#  define PADDING          1 /* padd locks/conditionals to cache-line */
#endif
#if !defined(LOCK_IN_COOP)
#  define LOCK_IN_COOP     0 /* spin for TTAS_MAX_SPINS before calling */
#endif
#if !defined(LOCK_IN_MAX_SPINS)
#  define LOCK_IN_MAX_SPINS 1024	/* sched_yield() to yield the cpu to others */
#endif
#define FREQ_CPU_GHZ      2.8
#define REPLACE_MUTEX     1	/* ovewrite the pthread_[mutex|cond] functions */

#define LOCK_IN_VERBOSE   0

#define LOCK_IN_CS_LAT    0

#if LOCK_IN_CS_LAT == 1
#  define LOCK_IN_CS_LAT_PRINT_EVERY ((128 * 1024) - 1)

static inline uint64_t
lock_in_cs_lat_getticks(void)
{
  unsigned hi, lo;
  asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
  return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

#  define LOCK_IN_CS_VARS					\
  uint64_t cs_n;						\
  volatile uint64_t cs_start;					\
  uint64_t cs_tot;						\
  uint64_t cs_padding[CACHE_LINE_SIZE - 3 * sizeof(uint64_t)]

#  define LOCK_IN_CS_START(lock)		\
  (lock)->cs_start = lock_in_cs_lat_getticks();
#  define LOCK_IN_CS_STOP(lock)					\
  volatile uint64_t cs_stop = lock_in_cs_lat_getticks();	\
  uint32_t cs_n = ++(lock)->cs_n;				\
  uint64_t cs_tot = ((lock)->cs_tot += cs_stop - (lock)->cs_start);	\
  if (__builtin_expect((cs_n & LOCK_IN_CS_LAT_PRINT_EVERY) == 0, 0))	\
    {									\
      printf("[cs stats|%16p] avg cs dur: %llu\n",			\
	     (lock), cs_tot / cs_n);					\
    }

#else
#  define LOCK_IN_CS_VARS					
#  define LOCK_IN_CS_START(lock)	       
#  define LOCK_IN_CS_STOP(lock)
#endif


#if defined(RTM)
#  if !defined(USE_HLE)
#    define USE_HLE       3	/* use hardware lock elision (hle)
				   0: don't use hle 
				   1: use plain hle
				   2: use rtm-based hle (pessimistic)
				   3: use rtm-based hle (optimistic)
				*/
#  endif
#  define HLE_RTM_OPT_RETRIES 3	/* num or retries if tx_start() fails */


#  if LOCK_IN_VERBOSE == 1
#    warning using hle USE_HLE
#  endif
#endif

#if defined(PAUSE_IN)
#  if PAUSE_IN == 0
#    undef PAUSE_IN
#    define PAUSE_IN() 
#    if LOCK_IN_VERBOSE == 1
#      warning   with empty
#    endif
#  elif PAUSE_IN == 1
#    undef PAUSE_IN
#    define PAUSE_IN() asm volatile("nop")
#    if LOCK_IN_VERBOSE == 1
#      warning   with nop
#    endif
#  elif PAUSE_IN == 2
#    undef PAUSE_IN
#    define PAUSE_IN() asm volatile("pause");
#    if LOCK_IN_VERBOSE == 1
#      warning   with pause
#    endif
#  elif PAUSE_IN == 3
#    undef PAUSE_IN
#    define PAUSE_IN() asm volatile("mfence");
#    if LOCK_IN_VERBOSE == 1
#      warning   with mfence
#    endif
#  elif PAUSE_IN == 4
#    undef PAUSE_IN
#    define PAUSE_IN() \
  asm volatile("nop"); asm volatile("nop"); asm volatile("nop");
#    if LOCK_IN_VERBOSE == 1
#      warning   with 3x nop
#    endif
#  elif PAUSE_IN == 5
#    undef PAUSE_IN
#    define PAUSE_IN() \
  ;
#    if LOCK_IN_VERBOSE == 1
#      warning   with none
#    endif
#  endif
#endif

#if !defined(PAUSE_IN)
#  define PAUSE_IN()  	 /* empty by default */
#endif

/* ******************************************************************************** */
/* algorithms ********************************************************************* */
#define MUTEX        1		/* pthread mutex lock */
#define MUTEXADAP    2	   /* pthread mutex lock - adaptive flag on */
#define TAS          3		/* test-and-set spinlock */
#define TTAS         4		/* test-and-test-and-set spinlock */
#define TICKET       5		/* ticket lock */
#define MCS          6		/* MCS algorithm */
#define CLH          7		/* CLH algorithm */
#define MUTEXEE      8		/* MUTEXEE w/o  fairness code */
#define MUTEXEEF     9		/* MUTEXEE with fairness code */
#define LOCKPROF     10		/* a profiler ticket lock */

  /* various efforrts to get energy efficienty locks */
  /* !!!!!!!!!!!!!!!!!!!!!!!!! not thoroughly tested */
  /* !!!!!!!!!!!!!!!!!!!!!!!!! not thoroughly tested */
  /* !!!!!!!!!!!!!!!!!!!!!!!!! not thoroughly tested */
#define LADAP        11
#define LADAP1       12
#define LADAP2       13
#define LADAP3       14
#define LADAP4       15
#define LADAP5       16
#define TICKETFU     17
#define TICKETLINUX  18		/* ticket lock (as implemented in 
				   the linux kernel */
#define TICKETDVFS   19
#define TTASSHARED   20		

#if LOCK_IN == CLH
#  if LOCK_IN_VERBOSE == 1
#    warning using clh
#  endif
#  include "clh_in.h"
#  include "ttas_rw_in.h"
#elif LOCK_IN == MCS
#  if LOCK_IN_VERBOSE == 1
#    warning using mcs
#  endif
#  include "mcs_in.h"
#  include "ttas_rw_in.h"
#elif LOCK_IN == TAS
#  if LOCK_IN_VERBOSE == 1
#    warning using tas
#  endif
#  include "tas_in.h"
#  include "tas_rw_in.h"
#elif LOCK_IN == TTAS
#  if LOCK_IN_VERBOSE == 1
#    warning using ttas
#  endif
#  include "ttas_in.h"
#  include "ttas_rw_in.h"
#elif LOCK_IN == TTASSHARED
#  if LOCK_IN_VERBOSE == 1
#    warning using ttas-shared
#  endif
#  include "ttas_shared_in.h"
#elif LOCK_IN == TICKET
#  if LOCK_IN_VERBOSE == 1
#    warning using ticket
#  endif
#  include "ticket_in.h"
#  include "ttas_rw_in.h"
#elif LOCK_IN == TICKETLINUX
#  if LOCK_IN_VERBOSE == 1
#    warning using ticket-linux
#  endif
#  include "ticket_linux_in.h"
#  include "ttas_rw_in.h"
#elif LOCK_IN == MUTEX
#  if LOCK_IN_VERBOSE == 1
#    warning using mutex
#  endif
#  include "mutex_in.h"
#elif LOCK_IN == MUTEXADAP
#  if LOCK_IN_VERBOSE == 1
#    warning using mutex-adap
#  endif
#  include "mutex_adap_in.h"
#elif LOCK_IN == LOCKPROF
#  if LOCK_IN_VERBOSE == 1
#    warning using lockprof
#  endif
#  include "lock_prof_in.h"
#  include "ttas_rw_in.h"
#elif LOCK_IN == LADAP
#  if LOCK_IN_VERBOSE == 1
#    warning using ladap
#  endif
#  include "ladap_in.h"
#  include "ttas_rw_in.h"
#elif LOCK_IN == LADAP1
#  if LOCK_IN_VERBOSE == 1
#    warning using ladap1
#  endif
#  include "ladap1_in.h"
#  include "ttas_rw_in.h"
#elif LOCK_IN == LADAP2
#  if LOCK_IN_VERBOSE == 1
#    warning using ladap2
#  endif
#  include "ladap2_in.h"
#  include "ttas_rw_in.h"
#elif LOCK_IN == LADAP3
#  if LOCK_IN_VERBOSE == 1
#    warning using ladap3
#  endif
#  include "ladap3_in.h"
#  include "ttas_rw_in.h"
#elif LOCK_IN == LADAP4
#  if LOCK_IN_VERBOSE == 1
#    warning using ladap4
#  endif
#  include "ladap4_in.h"
#  include "ttas_rw_in.h"
#elif LOCK_IN == LADAP5
#  if LOCK_IN_VERBOSE == 1
#    warning using ladap5
#  endif
#  include "ladap5_in.h"
#  include "ttas_rw_in.h"
#elif LOCK_IN == TICKETFU
#  if LOCK_IN_VERBOSE == 1
#    warning using ticketfu
#  endif
#  include "ticket_fu_in.h"
#  include "ttas_rw_in.h"
#elif LOCK_IN == TICKETDVFS
#  if LOCK_IN_VERBOSE == 1
#    warning using tickedvfs
#  endif
#  include "ticket_dvfs_in.h"
#  include "ttas_rw_in.h"
#elif LOCK_IN == MUTEXEE
#  if LOCK_IN_VERBOSE == 1
#    warning using mutexee
#  endif
#  include "mutexee_in.h"
#  include "ttas_rw_in.h"
#elif LOCK_IN == MUTEXEEF
#  if LOCK_IN_VERBOSE == 1
#    warning using mutexeef
#  endif
#  define MUTEXEE_FAIR 1
#  include "mutexee_in.h"
#  include "ttas_rw_in.h"
#else
#  error tell me which lock to use
#endif

static inline const char*
lock_in_lock_name()
{
  return LOCK_IN_NAME;
}

#ifdef __cplusplus
}
#endif

#endif	/* _LOCK_IN_H_ */
