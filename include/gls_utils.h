/*   
 * File: gls_utils.h
 * Authors: Georgios Chatzopoulos <georgios.chatzopoulos@epfl.ch>
 *          Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description: 
 *      Utilities for GLS.
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Georgios Chatzopoulos, Vasileios Trigonakis
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
 *
 */


#ifndef _GLS_UTILS_H_INCLUDED_
#define _GLS_UTILS_H_INCLUDED_
extern __thread double __zipf_norm_constant;
extern __thread int __zipf_initialized;
extern __thread unsigned long* __zipf_seeds;
#define ZIPF_RAND_DECLARATIONS()                \
  __thread double __zipf_norm_constant = 0;     \
  __thread int __zipf_initialized = 0;          \
  __thread unsigned long* __zipf_seeds = NULL;



static inline double
zipf_init(const double alpha, const int max)
{
  if (unlikely(__zipf_initialized == 0))
    {
      __zipf_seeds = seed_rand();

      int i;
      for (i=1; i <= max; i++)
        {
          __zipf_norm_constant = __zipf_norm_constant + (1.0 / pow((double) i, alpha));
        }
      __zipf_norm_constant = 1.0 / __zipf_norm_constant;
      __zipf_initialized = 1;
    }
  return __zipf_norm_constant;
}


static inline int
zipf(double alpha, const int max)
{
  static double c = 0;          // Normalization constant
  double z;                     // Uniform random number (0 < z < 1)
  double sum_prob;              // Sum of probabilities
  double zipf_value = 0;        // Computed exponential value to be returned

  // Compute normalization constant on first call only
  c = zipf_init(alpha, max);

  // Pull a uniform random number (0 < z < 1)
  do
    {
      z = my_random(&(__zipf_seeds[0]),&(__zipf_seeds[1]),&(__zipf_seeds[2])) / (double) ((unsigned long) (-1));
    }
  while ((z == 0) || (z == 1));

  // Map z to the value
  sum_prob = 0;
  int i;
  for (i = 1; i <= max; i++)
    {
      sum_prob = sum_prob + c / pow((double) i, alpha);
      if (sum_prob >= z)
        {
          zipf_value = i;
          break;
        }
    }

  // Assert that zipf_value is between 1 and N
  /* assert((zipf_value >=1) && (zipf_value <= max)); */

  return (zipf_value - 1);
}




static inline int*
zipf_get_rand_array(double zipf_alpha, const size_t num_vals, const int max)
{
  int* _zipf_vals = (int*) malloc(num_vals * sizeof(int));
  assert(_zipf_vals != NULL);
  int i;
  for (i = 0; i < num_vals; i++)
    {
      _zipf_vals[i] = zipf(zipf_alpha, max);
    }
  return _zipf_vals;
}


#endif
