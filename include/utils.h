/*
 * File: utils.h
 * Authors: Tudor David <tudor.david@epfl.ch>,
 *          Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description: 
 *      Some utility functions
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013 Tudor David, Vasileios Trigonakis
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

#ifndef _UTILS_H_INCLUDED_
#define _UTILS_H_INCLUDED_

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <malloc.h>
#include <sched.h>
#include <inttypes.h>
#include <sys/time.h>
#include <unistd.h>
#ifdef __sparc__
#  include <sys/types.h>
#  include <sys/processor.h>
#  include <sys/procset.h>
#elif defined(__tile__)
#  include <arch/atomic.h>
#  include <arch/cycle.h>
#  include <tmc/cpus.h>
#  include <tmc/task.h>
#  include <tmc/spin.h>
#  include <sched.h>
#else
#  include <emmintrin.h>
#  include <xmmintrin.h>
#  include <numa.h>
#endif
#include <pthread.h>

#include "platform_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ALIGNED(N) __attribute__ ((aligned (N)))
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#if !defined(UNUSED)
#  define UNUSED __attribute__ ((unused))
#endif

#ifdef __sparc__
#  define PAUSE    asm volatile("rd    %%ccr, %%g0\n\t" \
				::: "memory")

#elif defined(__tile__)
#  define PAUSE cycle_relax()
#else
#  define PAUSE _mm_pause()
#endif
  static inline void
  pause_rep(uint32_t num_reps)
  {
    uint32_t i;
    for (i = 0; i < num_reps; i++)
      {
	PAUSE;
	/* PAUSE; */
	/* asm volatile ("NOP"); */
      }
  }

  static inline void
  nop_rep(uint32_t num_reps)
  {
    uint32_t i;
    for (i = 0; i < num_reps; i++)
      {
	asm volatile ("NOP");
      }
  }


  //debugging functions
#ifdef DEBUG
#  define DPRINT(args...) fprintf(stderr,args);
#  define DDPRINT(fmt, args...) printf("%s:%s:%d: "fmt, __FILE__, __FUNCTION__, __LINE__, args)
#else
#  define DPRINT(...)
#  define DDPRINT(fmt, ...)
#endif




  typedef uint64_t ticks;

  static inline double wtime(void)
  {
    struct timeval t;
    gettimeofday(&t,NULL);
    return (double)t.tv_sec + ((double)t.tv_usec)/1000000.0;
  }

  static inline int
  set_cpu(int cpu) 
  {
    int ret = 1;
#if defined(__sparc__)
    if (processor_bind(P_LWPID, P_MYID, cpu, NULL) == -1)
      {
    /* printf("Problem with setting processor affinity: %s\n", */
    /* 	     strerror(errno)); */
    ret = 0;
  }
#elif defined(__tile__)
    if (tmc_cpus_set_my_cpu(tmc_cpus_find_nth_cpu(&cpus, cpu)) < 0)
      {
	tmc_task_die("Failure in 'tmc_cpus_set_my_cpu()'.");
      }

    if (cpu != tmc_cpus_get_my_cpu())
      {
	PRINT("******* i am not CPU %d", tmc_cpus_get_my_cpu());
      }

#else
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu, &mask);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &mask) != 0)
      {
    /* printf("Problem with setting processor affinity: %s\n", */
    /* 	     strerror(errno)); */
    ret = 0;
  }
#endif

    return ret;
  }

#if defined(__i386__)
  static inline ticks getticks(void) {
    ticks ret;

    __asm__ __volatile__("rdtsc" : "=A" (ret));
    return ret;
  }
#elif defined(__x86_64__)
  static inline ticks getticks(void)
  {
    unsigned hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
  }
#elif defined(__sparc__)
  static inline ticks getticks(){
    ticks ret;
    __asm__ __volatile__ ("rd %%tick, %0" : "=r" (ret) : "0" (ret)); 
    return ret;
  }
#elif defined(__tile__)
  static inline ticks getticks(){
    return get_cycle_count();
  }
#endif

  static inline void cdelay(ticks cycles)
  {
    ticks cy = cycles;
    while (cy--)
      {
	asm volatile ("");
      }
  }

  static inline void cpause(ticks cycles)
  {
    cdelay(cycles);
  }

  static inline void udelay(unsigned int micros)
  {
    double __ts_end = wtime() + ((double) micros / 1000000);
    while (wtime() < __ts_end);
  }

  //getticks needs to have a correction because the call itself takes a
  //significant number of cycles and skewes the measurement
  static inline ticks getticks_correction_calc() {
#define GETTICKS_CALC_REPS 5000000
    ticks t_dur = 0;
    uint32_t i;
    for (i = 0; i < GETTICKS_CALC_REPS; i++) {
      ticks t_start = getticks();
      ticks t_end = getticks();
      t_dur += t_end - t_start;
    }
    //    printf("corr in float %f\n", (t_dur / (double) GETTICKS_CALC_REPS));
    ticks getticks_correction = (ticks)(t_dur / (double) GETTICKS_CALC_REPS);
    return getticks_correction;
  }

  static inline ticks get_noop_duration() {
#define NOOP_CALC_REPS 1000000
    ticks noop_dur = 0;
    uint32_t i;
    ticks corr = getticks_correction_calc();
    ticks start;
    ticks end;
    start = getticks();
    for (i=0;i<NOOP_CALC_REPS;i++) {
      __asm__ __volatile__("nop");
    }
    end = getticks();
    noop_dur = (ticks)((end-start-corr)/(double)NOOP_CALC_REPS);
    return noop_dur;
  }

  /// Round up to next higher power of 2 (return x if it's already a power
  /// of 2) for 32-bit numbers
  static inline uint32_t pow2roundup (uint32_t x){
    if (x==0) return 1;
    --x;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x+1;
  }
#define my_random xorshf96

  /* 
   * Returns a pseudo-random value in [1;range).
   * Depending on the symbolic constant RAND_MAX>=32767 defined in stdlib.h,
   * the granularity of rand() could be lower-bounded by the 32767^th which might 
   * be too high for given values of range and initial.
   */
  static inline long rand_range(long r) {
    int m = RAND_MAX;
    long d, v = 0;

    do {
      d = (m > r ? r : m);
      v += 1 + (long) (d * ((double) rand() / ((double) (m) + 1.0)));
      r -= m;
    } while (r > 0);
    return v;
  }

  //fast but weak random number generator for the sparc machine
  static inline uint32_t fast_rand() {
    return ((getticks()&4294967295)>>4);
  }


  static inline unsigned long* 
  seed_rand() 
  {
    unsigned long* seeds;
    int num_seeds = CACHE_LINE_SIZE/sizeof(unsigned long);
    if (num_seeds<3) num_seeds=3;
    seeds = (unsigned long*) memalign(CACHE_LINE_SIZE, num_seeds * sizeof(unsigned long));
    seeds[0] = getticks() % 123456789;
    seeds[1] = getticks() % 362436069;
    seeds[2] = getticks() % 521288629;
    return seeds;
  }

  //Marsaglia's xorshf generator
  static inline unsigned long xorshf96(unsigned long* x, unsigned long* y, unsigned long* z) {          //period 2^96-1
    unsigned long t;
    (*x) ^= (*x) << 16;
    (*x) ^= (*x) >> 5;
    (*x) ^= (*x) << 1;

    t = *x;
    (*x) = *y;
    (*y) = *z;
    (*z) = t ^ (*x) ^ (*y);

    return *z;
  }

#ifdef __cplusplus
}

#endif


static inline size_t
parse_size(char* optarg)
{
  size_t test_mem_size_multi = 1;
  char multi = optarg[strlen(optarg) - 1];
  if (multi == 'b' || multi == 'B')
    {
      optarg[strlen(optarg) - 1] = optarg[strlen(optarg)];
      multi = optarg[strlen(optarg) - 1];
    }

  if (multi == 'k' || multi == 'K')
    {
      test_mem_size_multi = 1024;
      optarg[strlen(optarg) - 1] = optarg[strlen(optarg)];
    }
  else if (multi == 'm' || multi == 'M')
    {
      test_mem_size_multi = 1024 * 1024LL;
      optarg[strlen(optarg) - 1] = optarg[strlen(optarg)];
    }
  else if (multi == 'g' || multi == 'G')
    {
      test_mem_size_multi = 1024 * 1024 * 1024LL;
      optarg[strlen(optarg) - 1] = optarg[strlen(optarg)];
    }

  return test_mem_size_multi * atoi(optarg);
}

static inline int
hw_ctx_nr_get()
{
  return sysconf(_SC_NPROCESSORS_ONLN);
}

#endif
