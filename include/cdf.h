/*   
 *   File: cdf.h
 *   Author: Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *   Description: cummulative distribution function + clustering of
 *                values
 *
 */

#ifndef _CDF_H_
#define _CDF_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <inttypes.h>
#include <assert.h>


typedef struct 
{
  size_t x;
  double cdf;
} cdf_pair_t;

#define CDF_BOXPLOT_VALS 7

typedef struct
{
  double confidence;
  size_t limits[CDF_BOXPLOT_VALS];
  size_t values[CDF_BOXPLOT_VALS];
} cdf_boxplot_t;

typedef struct cdf_stats_plus
{
  double avg;
  double stdev;
  double stdevp;
} cdf_stats_plus_t;

typedef struct 
{
  size_t* vals_sorted;
  size_t val_n;
  size_t pair_n;
  cdf_pair_t* pairs;
  cdf_stats_plus_t plus;
} cdf_t;

typedef struct 
{
  size_t x_min;
  size_t x_max;
} cdf_clust_t;

typedef struct 
{
  size_t clust_n;
  cdf_clust_t* clusts;
} cdf_clustering_t;


cdf_t* cdf_calc(const size_t* vals, const size_t val_n);
cdf_t* cdf_calc_plus(const size_t* vals, const size_t val_n);
void cdf_print(const cdf_t* e);
void cdf_print_boxplot(const cdf_t* e, const double perc, const char* title);
void cdf_print_boxplot_limits(const cdf_t* e, const double* limits, const char* title);
void cdf_print_avg(const cdf_t* e);
void cdf_plot(const cdf_t* e);
cdf_clustering_t* cdf_cluster(const cdf_t* e, const int cluster_offset);
size_t cdf_clust_get_val(const cdf_clustering_t* c, const size_t val);
void cdf_destroy(cdf_t* e);
void cdf_clustering_destroy(cdf_clustering_t* c);

void cdf_boxplot_print(const cdf_boxplot_t* b, const char* title);
void cdf_boxplot_get(cdf_boxplot_t* b, const cdf_t* e, const double perc);
size_t cdf_boxplot_get_median(cdf_boxplot_t* b);
size_t cdf_boxplot_get_min(cdf_boxplot_t* b);
void cdf_boxplot_diff(cdf_boxplot_t* d, const cdf_boxplot_t* a, const cdf_boxplot_t* b);
void cdf_boxplot_minus(cdf_boxplot_t* d, const size_t minus);


#endif
