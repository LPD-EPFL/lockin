#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

static inline double
cpu_freq_get()
{
  volatile size_t r;
  for (r = 0; r < 1e6; r++) 
    {
      asm volatile ("");
    }

  clock_t c1 = clock();
  volatile ticks t1 = getticks();
  for (r = 0; r < 2e8; r++) 
    {
      asm volatile ("");
    }
  volatile ticks t2 = getticks();
  clock_t c2 = clock();
  double sec = (c2 - c1)/(double)CLOCKS_PER_SEC;
  double freq =  (t2 - t1) / (sec * 1e9);

  return (((int)(1000*freq) / 1000.0));
}

static inline void
print_space(int n)
{
  int i;
  for (i = 0; i < n; i++) 
    {
      printf(" ");
    }
}

#endif
