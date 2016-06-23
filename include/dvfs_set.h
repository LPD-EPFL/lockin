#ifndef _DVFS_SET_H_
#define _DVFS_SET_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <time.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <string.h>
#include <linux/futex.h>
#include <unistd.h>
#include <sched.h>

#if defined(XEON)
static const char* dvfs_freq_max = "2800000";
static const char* dvfs_freq_min = "1200000";
#else  /* desktop */
static const char* dvfs_freq_max = "3300000";
static const char* dvfs_freq_min = "800000 ";
#endif

typedef enum
  {
    DVFS_FREQ_MIN,
    DVFS_FREQ_MAX,
  } dvfs_setting_t;

#define DVFS_STR_LEN 8

  extern int dvfs_freq_init(int cid);
  extern int dvfs_freq_term(int cpu);
  extern int dvfs_freq_set_cpu(int cpu, const char* freq_khz);
  extern int dvfs_freq_set(const char* freq);
  extern inline int dvfs_freq_set_min();
  extern inline int dvfs_freq_set_max();
  extern int dvfs_freq_set_all_max();
  extern int dvfs_freq_set_range(int core_from, int core_to, dvfs_setting_t setting);

  static inline int
  dvfs_freq_set_min()
  {
    return dvfs_freq_set(dvfs_freq_min);
  }

  static inline int
  dvfs_freq_set_max()
  {
    return dvfs_freq_set(dvfs_freq_max);
  }



#ifdef __cplusplus
}
#endif

#endif	/* _DVFS_SET_H_ */
