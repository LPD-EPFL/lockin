/*
 * File: cdf.h
 * Author: Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 *   Description: calculate and plot the CDF of an array of values
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

#include "cdf.h"
#include <math.h>

static int
cdf_comp(const void *elem1, const void *elem2) 
{
  size_t f = *((size_t*)elem1);
  size_t s = *((size_t*)elem2);
  if (f > s) return  1;
  if (f < s) return -1;
  return 0;
}


cdf_t*
cdf_calc(const size_t* vals, const size_t val_n)
{
  cdf_t* e = (cdf_t*) calloc(1, sizeof(cdf_t));
  assert(e != NULL);

  size_t* vals_sorted = (size_t*) malloc(val_n * sizeof(size_t));
  assert(vals_sorted != NULL);

  e->vals_sorted = vals_sorted;

  memcpy(vals_sorted, vals, val_n * sizeof(size_t));
  qsort(vals_sorted, val_n, sizeof(size_t), cdf_comp);

  cdf_pair_t* ps = (cdf_pair_t*) malloc(val_n * sizeof(cdf_pair_t));
  assert(ps != NULL);

  size_t ps_n = 0;

  size_t cur = vals_sorted[0];

  int i;
  for (i = 1; i < val_n; i++)
    {
      if (cur != vals_sorted[i])
	{
	  ps[ps_n].x = cur;
	  ps[ps_n].cdf = ((double) i / val_n) * 100.0;
	  ps_n++;
	  cur = vals_sorted[i];
	}
    }

  ps[ps_n].x = cur;
  ps[ps_n].cdf = 100.0;
  ps_n++;

  e->val_n = val_n;
  e->pair_n = ps_n;
  e->pairs = (cdf_pair_t*) malloc(ps_n * sizeof(cdf_pair_t));
  assert(e->pairs != NULL);

  for (i = 0; i < e->pair_n; i++)
    {
      e->pairs[i].x = ps[i].x;
      e->pairs[i].cdf = ps[i].cdf;
    }

  free(ps);
  return e;
}

void
cdf_print(const cdf_t* e)
{
  int cdf_cur = 0;
  int p;
  for (p = 0; p < e->pair_n; p++)
    {
      int cdf = (int) e->pairs[p].cdf;
      if (cdf > cdf_cur)
	{
	  printf("%-5d : %-5zu -> %f%%\n", p, e->pairs[p].x, e->pairs[p].cdf);
	  cdf_cur = cdf;
	}
    }
}


void
cdf_print_boxplot_limits(const cdf_t* e, const double* limits, const char* title)
{
  const double* target = limits;

  int target_cur = 0;
  printf("#CDF  %10s", "");
  int p;
  for (p = 0; p < CDF_BOXPLOT_VALS; p++)
    {
      printf("%9.1f%% ", target[p]);
    }

  printf("\n#CDF-%-10s ", title);

  for (p = 0; p < e->pair_n && target_cur < CDF_BOXPLOT_VALS; p++)
    {
      double cdf = e->pairs[p].cdf;
      if (cdf >= target[target_cur])
	{
	  target_cur++;
	  printf("%10zu ", e->pairs[p].x);
	  p--;
	}
    }
  printf("\n");
  /* printf("#CDF-%-10s ", title); */
  /* for (p = 0; p < CDF_BOXPLOT_VALS; p++) */
  /*   { */
  /*     int ei = e->val_n * (target[p] / 100.0); */
  /*     printf("%10zu ", e->vals_sorted[ei]); */
  /*   } */
  /* printf("\n"); */

  
}

void
cdf_boxplot_get(cdf_boxplot_t* b, const cdf_t* e, const double perc)
{
  double target[5] = { 100 - perc, 25, 50, 75, perc };
  int target_cur = 0;

  b->confidence = perc;
  b->values[target_cur] =  e->pairs[0].x;
  int p;
  for (p = 0; p < e->pair_n && target_cur < 5; p++)
    {
      double cdf = e->pairs[p].cdf;
      if (cdf >= target[target_cur])
	{
	  target_cur++;
	  b->values[target_cur] = e->pairs[p].x;
	  p--;
	}
    }
  b->values[CDF_BOXPLOT_VALS - 1] = e->pairs[e->pair_n - 1].x;
}

size_t
cdf_boxplot_get_median(cdf_boxplot_t* b)
{
  return b->values[(CDF_BOXPLOT_VALS / 2)];
}

size_t
cdf_boxplot_get_min(cdf_boxplot_t* b)
{
  return b->values[0];
}

void
cdf_boxplot_diff(cdf_boxplot_t* d, const cdf_boxplot_t* a, const cdf_boxplot_t* b)
{
  assert(a->confidence == b->confidence);
  d->confidence = a->confidence;
  int p;
  for (p = 0; p < CDF_BOXPLOT_VALS; p++)
    {
      d->values[p] = b->values[p] - a->values[p];
    }
}

void
cdf_boxplot_minus(cdf_boxplot_t* d, const size_t minus)
{
  int p;
  for (p = 0; p < CDF_BOXPLOT_VALS; p++)
    {
      if (d->values[p] >= minus)
	{
	  d->values[p] -= minus;
	}
      else
	{
	  d->values[p] = 0;
	}
    }
}

void
cdf_boxplot_print(const cdf_boxplot_t* b, const char* title)
{
  double target[5] = { 100 - b->confidence, 25, 50, 75, b->confidence };

  printf("#CDF  ");
  printf("%20s ", "min");
  int p;
  for (p = 0; p < 5; p++)
    {
      printf("%9.1f%% ", target[p]);
    }

  printf("%10s ", "max");
  printf("\n#CDF-%-10s ", title);

  for (p = 0; p < CDF_BOXPLOT_VALS; p++)
    {
      printf("%10zu ", b->values[p]);
    }
  printf("\n");
}

void
cdf_print_avg(const cdf_t* e)
{
  printf("#AVG         %-10s %-10s %-10s\n", "mean", "stdev", "stdev%");
  printf("#AVGv        %-10.0f %-10.1f %-10.3f\n", e->plus.avg, e->plus.stdev, e->plus.stdevp);
}

void
cdf_destroy(cdf_t* e)
{
  free(e->pairs);
  free(e);
}

