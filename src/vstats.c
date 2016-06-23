#include "cdf.h"
#include "math.h"

#define DBL_LARGE 1e9

static int
vstat_comp(const void *elem1, const void *elem2) 
{
  double f = *((double*)elem1);
  double s = *((double*)elem2);
  if (f > s) return  1;
  if (f < s) return -1;
  return 0;
}

static double
sqr(double v)
{
  return v * v;
}


int 
main(int argc, char** argv)
{
  size_t val_n = argc - 1;
  double* vals = (double*) calloc(val_n, sizeof(double));
  assert(vals != NULL);

  int i;
  for (i = 1; i < argc; i++)
    {
      double v = atof(argv[i]);
      if (v > 0)
	{
	  vals[i - 1] = v;
	}
      else
	{
	  val_n--;
	}
    }

  if (val_n == 0)
    {
      printf("%-4zu %-10f %-10f %-10f %-10f\n", val_n, 0.0, 0.0, DBL_LARGE, DBL_LARGE);
    }
  else
    {
      qsort(vals, val_n, sizeof(double), vstat_comp);

      double sum = 0;
      for (i = 0; i < val_n; i++)
	{
	  sum += vals[i];
	}

      double mean = sum / val_n;
      int median_idx = val_n / 2;
      double median = vals[median_idx];

      //sqrt((1/N)*(sum of (value - mean)^2))
      double std_sum = 0;
      for (i = 0; i < val_n; i++)
	{
	  std_sum += sqr(vals[i] - mean);
	}
      double std = sqrt(std_sum / val_n);

      double stdp = 100 * (1 - ((mean - std) / mean));

      printf("%-4zu %-10f %-10f %-10f %-10f\n", val_n, median, mean, std, stdp);
    }
  free(vals);
  return 0;
}


