#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include "utils.h"

#include "cdf.h"
#include "rapl_read.h"
#include "lock_in.h"

#define DELAY_NONE      0
#define DELAY_NORMAL    1
#define DELAY_FAIR      2
#define DELAY_FAIR_PLUS 3

#define DELAY_FOR_FAIRNESS 0

#define DELAY         DELAY_FAIR

#define STR(s) #s
#define XSTR(s) STR(s)

//number of concurres threads
#define DEFAULT_NUM_THREADS 1
//whether or not to set cpu
#define DEFAULT_SET_CPU 1
//total number of locks
#define DEFAULT_NUM_LOCKS 1
//number of lock acquisitions in this test
#define DEFAULT_ACQ_DELAY 0
//delay between lock acquire and release in cycles
#define DEFAULT_ACQ_DURATION 0
//delay (*num_threads) after a lock is release to ensure fairness
#define DEFAULT_FAIR_DELAY 30
#define DEFAULT_FAIR_DELAY_BASE 30
//delay between lock acquire and release in cycles
#define DEFAULT_CL_ACCESS 0
//the total duration of a test
#define DEFAULT_DURATION 1000
//if do_writes is 0, the test only reads cache lines, else it also writes them
#define DEFAULT_DO_WRITES 0
//which level of prints to do with RAPL
#define DEFAULT_POW_PRINT RAPL_PRINT_ENE
#define DEFAULT_VERBOSE 0
#define DEFAULT_BOXPLOT_PERC 95

static volatile int stop = 0;

__thread unsigned long* seeds;
__thread uint32_t phys_id;

typedef struct shared_data
{
  volatile char the_data[CACHE_LINE_SIZE];
} shared_data;


pthread_mutex_t* locks;
__attribute__((aligned(CACHE_LINE_SIZE))) volatile shared_data* protected_data;
__attribute__((aligned(CACHE_LINE_SIZE))) volatile uint32_t* protected_offsets;
int duration;
int num_locks;
int do_set_cpu;
int do_writes;
int num_threads;
int acq_duration;
int acq_delay;
int fair_delay;
int mutex_delay;
int cl_access;
int power_print;
int verbose;
double test_boxplot_perc;

typedef struct barrier 
{
  pthread_cond_t complete;
  pthread_mutex_t mutex;
  int count;
  int crossing;
} barrier_t;

void barrier_init(barrier_t *b, int n)
{
  pthread_cond_init(&b->complete, NULL);
  pthread_mutex_init(&b->mutex, NULL);
  b->count = n;
  b->crossing = 0;
}

void barrier_cross(barrier_t *b)
{
  pthread_mutex_lock(&b->mutex);
  /* One more thread through */
  b->crossing++;
  /* If not all here, wait */
  if (b->crossing < b->count) {
    pthread_cond_wait(&b->complete, &b->mutex);
  } else {
    pthread_cond_broadcast(&b->complete);
    /* Reset for next time */
    b->crossing = 0;
  }
  pthread_mutex_unlock(&b->mutex);
}


#define LDI_VALS_NUM ((1<<19))

typedef struct thread_data 
{
  union
  {
    struct
    {
      barrier_t *barrier;
      unsigned long num_acquires;
      unsigned long num_consecutive_acq;
      unsigned long fair_delay;
      ticks ticks_lock;
      ticks ticks_unlock;
      ticks* vals_lock;
      ticks* vals_unlock;
      int id;
    };
    char padding[CACHE_LINE_SIZE];
  };
} thread_data_t;


#if DELAY >= DELAY_FAIR
size_t shared_counter = 0;
#endif

void*
test(void* data)
{
  thread_data_t *d = (thread_data_t *)data;
  int rand_max = num_locks - 1;
  phys_id = the_cores[d->id];
  if (do_set_cpu && num_threads <= 40)
    {
      set_cpu(phys_id);
    }

  seeds = seed_rand();
#if DELAY >= DELAY_FAIR_PLUS
  size_t counter_prev = -1;
  size_t num_consecutive_acq = 0;
#endif


  UNUSED size_t sum = 0;
  size_t num_acquires = 0;

  d->vals_lock = (ticks*) calloc(LDI_VALS_NUM, sizeof(ticks));
  d->vals_unlock = (ticks*) calloc(LDI_VALS_NUM, sizeof(ticks));
  assert(d->vals_lock != NULL && d->vals_unlock != NULL);

  /* Wait on barrier */
  barrier_cross(d->barrier);

  while (stop == 0) 
    {
      int lock_to_acq = (int) my_random(&(seeds[0]),&(seeds[1]),&(seeds[2])) & rand_max;

      volatile ticks s_lock = getticks();
      pthread_mutex_lock(locks + lock_to_acq);      
      volatile ticks e_lock = getticks();

#if DELAY >= DELAY_FAIR_PLUS
      if (shared_counter++ == counter_prev)
	{
	  num_consecutive_acq++;
	}
      counter_prev = shared_counter;
#endif


#if DELAY >= DELAY_NORMAL
      if (acq_duration > 0)
	{
	  cpause(acq_duration);
	}
      uint32_t i;
      for (i = 0; i < cl_access; i++)
	{
	  if (do_writes == 1) 
	    {
	      protected_data[i + protected_offsets[lock_to_acq]].the_data[0] += d->id;
	    } 
	  else 
	    {
	      sum += protected_data[i + protected_offsets[lock_to_acq]].the_data[0];
	    }
        }
#endif

      volatile ticks s_unlock = getticks();
      pthread_mutex_unlock(locks + lock_to_acq);      
      volatile ticks e_unlock = getticks();

      ticks llock = (e_lock - s_lock);
      ticks lunlock = (e_unlock - s_unlock);
      d->ticks_lock += llock;
      d->ticks_unlock += lunlock;
      d->vals_lock[num_acquires & (LDI_VALS_NUM - 1)] = llock;
      d->vals_unlock[num_acquires & (LDI_VALS_NUM - 1)] = lunlock;

#if DELAY >= DELAY_NORMAL
      if (acq_delay > 0) 
	{
	  cpause(acq_delay);
	}
#endif

#if DELAY == DELAY_FAIR
      if (d->fair_delay)
	{
	  cpause(d->fair_delay);
	}
#endif

      num_acquires++;
    }

  d->num_acquires = num_acquires;
#if DELAY >= DELAY_FAIR_PLUS
  d->num_consecutive_acq = num_consecutive_acq;
#endif
  return NULL;
}


void catcher(int sig)
{
  static int nb = 0;
  printf("CAUGHT SIGNAL %d\n", sig);
  if (++nb >= 3)
    exit(1);
}


int
main(int argc, char **argv)
{
  struct option long_options[] = 
    {
      // These options don't set a flag
      {"help",                      no_argument,       NULL, 'h'},
      {"verbose",                   no_argument,       NULL, 'v'},
      {"locks",                     required_argument, NULL, 'l'},
      {"duration",                  required_argument, NULL, 'd'},
      {"num-threads",               required_argument, NULL, 'n'},
      {"set-cpu",                   required_argument, NULL, 's'},
      {"acquire",                   required_argument, NULL, 'a'},
      {"fair",                      required_argument, NULL, 'f'},
      {"pause",                     required_argument, NULL, 'p'},
      {"do_writes",                 required_argument, NULL, 'w'},
      {"clines",                    required_argument, NULL, 'c'},
      {"power",                     required_argument, NULL, 'o'},
      {"boxplot",                   required_argument, NULL, 'b'},
      {NULL, 0, NULL, 0}
    };

  int i, c;
  thread_data_t *data;
  pthread_t *threads;
  pthread_attr_t attr;
  barrier_t barrier;
  struct timeval start, end;
  struct timespec timeout;
  duration = DEFAULT_DURATION;
  num_locks = DEFAULT_NUM_LOCKS;
  do_set_cpu = DEFAULT_SET_CPU;
  do_writes = DEFAULT_DO_WRITES;
  num_threads = DEFAULT_NUM_THREADS;
  acq_duration = DEFAULT_ACQ_DURATION;
  fair_delay = DEFAULT_FAIR_DELAY;
  acq_delay = DEFAULT_ACQ_DELAY;
  cl_access = DEFAULT_CL_ACCESS;
  power_print = DEFAULT_POW_PRINT;
  verbose = DEFAULT_VERBOSE;
  test_boxplot_perc = DEFAULT_BOXPLOT_PERC;

  /* sigset_t block_set; */

  while(1) 
    {
      i = 0;
      c = getopt_long(argc, argv, "hvd:n:s:l:w:a:p:c:o:f:b:", long_options, &i);

      if(c == -1)
	break;

      if(c == 0 && long_options[i].flag == 0)
	c = long_options[i].val;

      switch(c)
	{
	case 0:
	  /* Flag is automatically set */
	  break;
	case 'h':
	  printf("lock stress test\n"
		 "\n"
		 "Usage:\n"
		 "  stress_test [options...]\n"
		 "\n"
		 "Options:\n"
		 "  -h, --help\n"
		 "        Print this message\n"
		 "  -l, --locks <int>\n"
		 "        Number of locks in the test (default=" XSTR(DEFAULT_NUM_LOCKS) ")\n"
		 "  -d, --duration <int>\n"
		 "        Test duration in milliseconds (0=infinite, default=" XSTR(DEFAULT_DURATION) ")\n"
		 "  -n, --num-threads <int>\n"
		 "        Number of threads (default=" XSTR(DEFAULT_NUM_THREADS) ")\n"
		 "  -s, --set-cpu <int>\n"
		 "        Pin threads to cores, or not (default=" XSTR(DEFAULT_SET_CPU) ")\n"
		 "  -a, --acquire <int>\n"
		 "        Number of cycles a lock is held (default=" XSTR(DEFAULT_ACQ_DURATION) ")\n"
		 "  -f, --fair <int>\n"
		 "        Number of pauses after a lock is released (to ensure fairness) (default=" XSTR(DEFAULT_FAIR_DELAY) ")\n"
		 "  -w, --do_writes <int>\n"
		 "        Whether or not the test writes cache lines (default=" XSTR(DEFAULT_DO_WRITES) ")\n"
		 "  -p, --pause <int>\n"
		 "        Number of cycles between a lock release and the next acquire (default=" XSTR(DEFAULT_ACQ_DELAY) ")\n"
		 "  -c, --clines <int>\n"
		 "        Number of cache lines written in every critical section (default=" XSTR(DEFAULT_CL_ACCESS) ")\n"
		 "  -o, --power <int>\n"
		 "        Print output level for RAPL power measurements (default=" XSTR(DEFAULT_POW_PRINT) ")\n"
		 "  -b, --boxplot <int>\n"
		 "        What's percentile latency values to print (default=" XSTR(DEFAULT_BOXPLOT_PERC) ")\n"
		 );
	  exit(0);
	case 'v':
	  verbose = 1;
	  break;
	case 'l':
	  num_locks = atoi(optarg);
	  break;
	case 'd':
	  duration = atoi(optarg);
	  break;
	case 'n':
	  num_threads = atoi(optarg);
	  break;
	case 's':
	  do_set_cpu = atoi(optarg);
	  break;
	case 'w':
	  do_writes = atoi(optarg);
	  break;
	case 'a':
#ifdef NO_DELAYS
#  ifdef PRINT_OUTPUT
	  printf("*** the NO_DELAYS flag is set");
#  endif
#endif
	  acq_duration = atoi(optarg);
	  break;
	case 'p':
#ifdef NO_DELAYS
#  ifdef PRINT_OUTPUT
	  printf("*** the NO_DELAYS flag is set");
#  endif
#endif
	  acq_delay = atoi(optarg);
	  break;
	case 'c':
	  cl_access = atoi(optarg);
	  break;
	case 'f':
	  fair_delay = atoi(optarg);
	  break;
	case 'o':
	  power_print = atoi(optarg);
	  break;
	case 'b':
	  test_boxplot_perc = atof(optarg);
	  break;
	case '?':
	  printf("Use -h or --help for help\n");
	  exit(0);
	default:
	  exit(1);
	}
    }
  mutex_delay=(num_threads-1) * 30;
  acq_delay=acq_delay;
  acq_duration=acq_duration;
  num_locks=pow2roundup(num_locks);
  assert(duration >= 0);
  assert(num_locks >= 1);
  assert(num_threads > 0);
  assert(acq_duration >= 0);
  assert(acq_delay >= 0);
  assert(cl_access >= 0);

  if (cl_access > 0)
    {
      protected_data = (shared_data*) calloc(cl_access * num_locks, sizeof(shared_data));
      protected_offsets = (uint32_t*) calloc(num_locks, sizeof(shared_data));
      int j;
      for (j = 0; j < num_locks; j++) 
	{
	  protected_offsets[j] = cl_access * j;
	}
    }

  locks = (pthread_mutex_t*) malloc(num_locks * sizeof(pthread_mutex_t));
  assert(locks != NULL);

  pthread_mutexattr_t mutex_attr;
  pthread_mutexattr_init(&mutex_attr);

  int l;
  for (l = 0; l < num_locks; l++)
    {
      pthread_mutex_init(locks + l, &mutex_attr);
    }

  pthread_mutexattr_destroy(&mutex_attr);
  

  if (verbose)
    {
      printf("Number of locks        : %d\n", num_locks);
      printf("Duration               : %d\n", duration);
      printf("Number of threads      : %d\n", num_threads);
      printf("Lock is held for       : %d\n", acq_duration);
      printf("Delay between locks    : %d\n", acq_delay);
      printf("Cache lines accessed   : %d\n", cl_access);
      printf("Do writes              : %d\n", do_writes);
      printf("sizeof(lock)           : %zu\n", sizeof(pthread_mutex_t));
    }
  timeout.tv_sec = duration / 1000;
  timeout.tv_nsec = (duration % 1000) * 1000000;


  if ((data = (thread_data_t *)malloc(num_threads * sizeof(thread_data_t))) == NULL) 
    {
      perror("malloc");
      exit(1);
    }
  if ((threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t))) == NULL) {
    perror("malloc");
    exit(1);
  }

  printf("## lock algo : %s\n", lock_in_lock_name());
  fair_delay = ((num_threads != 1) * DEFAULT_FAIR_DELAY_BASE) + (fair_delay * (num_threads - 1));
  printf("## fair delay: %d\n", fair_delay);

  if(duration > 0)
    {
      stop = 0;
    }

  /* Access set from all threads */
  barrier_init(&barrier, num_threads + 1);
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  for (i = 0; i < num_threads; i++) 
    {
      data[i].id = i;
      data[i].num_acquires = 0;
      data[i].num_consecutive_acq = 0;
      data[i].fair_delay = fair_delay;
      data[i].barrier = &barrier;
      if (pthread_create(&threads[i], &attr, test, (void *)(&data[i])) != 0) 
	{
	  fprintf(stderr, "Error creating thread\n");
	  exit(1);
	}
    }
  pthread_attr_destroy(&attr);

  /* Catch some signals */
  if (signal(SIGHUP, catcher) == SIG_ERR ||
      signal(SIGINT, catcher) == SIG_ERR ||
      signal(SIGTERM, catcher) == SIG_ERR)
    {
      perror("signal");
      exit(1);
    }

  RR_INIT_ALL();

  /* Start threads */
  barrier_cross(&barrier);

  gettimeofday(&start, NULL);

  RR_START_UNPROTECTED_ALL();
  if (duration > 0) 
    {
      nanosleep(&timeout, NULL);
    } 
  stop = 1;
  RR_STOP_UNPROTECTED_ALL();

  gettimeofday(&end, NULL);
  /* Wait for thread completion */
  for (i = 0; i < num_threads; i++) 
    {
      if (pthread_join(threads[i], NULL) != 0) 
	{
	  fprintf(stderr, "Error waiting for thread completion\n");
	  exit(1);
	}
    }

  if (verbose)
    {
      for (i = 0; i < cl_access * num_threads; i++)
	{
	  printf("%d ", protected_data[i].the_data[0]);
	}
      if (cl_access > 0)
	{
	  printf("\n");
	}
    }
  duration = (end.tv_sec * 1000 + end.tv_usec / 1000) - (start.tv_sec * 1000 + start.tv_usec / 1000);

  unsigned long acquires = 0;
  unsigned long consecutive_acq = 0;
  ticks ticks_lock = 0, ticks_unlock = 0;
  ticks* vals_lock = (ticks*) calloc(num_threads * LDI_VALS_NUM, sizeof(ticks));
  ticks* vals_unlock = (ticks*) calloc(num_threads * LDI_VALS_NUM, sizeof(ticks));
  assert(vals_lock != NULL && vals_unlock != NULL);
  size_t vals_idx = 0;

  for (i = 0; i < num_threads; i++) 
    {
      if (verbose)
	{
	  printf("Thread: %3d : #acquire   : %lu\n", i, data[i].num_acquires);
#if DELAY == DELAY_FAIR
	  printf("              #consequ   : %lu\n", data[i].num_consecutive_acq);
#endif
	}
      acquires += data[i].num_acquires;
      consecutive_acq += data[i].num_consecutive_acq;
      ticks_lock += data[i].ticks_lock;
      ticks_unlock += data[i].ticks_unlock;
      int v;
      for (v = 0; v < LDI_VALS_NUM; v++)
	{
	  if (data[i].vals_lock[v])
	    {
	      vals_lock[vals_idx] = data[i].vals_lock[v];
	      vals_unlock[vals_idx] = data[i].vals_unlock[v];
	      vals_idx++;
	    }
	  else
	    {
	      break;
	    }
	}
    }

  if (verbose)
    {
      printf("Duration      : %d (ms)\n", duration);
    }

  RR_PRINT_UNPROTECTED(RAPL_PRINT_POW);

  ticks_lock /= acquires;
  size_t ticks_lock_pc = ticks_lock / num_threads;
  ticks_unlock /= acquires;

  double thr = (double) (acquires * 1000.0 / duration);
  printf("#acquires     : %10lu ( %10.0f / s)\n", acquires, thr);
  printf("#lock ticks   : %-10llu = %-10zu per core\n", 
	 (unsigned long long) ticks_lock, ticks_lock_pc);
  printf("#unlock ticks : %-10llu\n", (unsigned long long) ticks_unlock);
#if DELAY >= DELAY_FAIR_PLUS
  double thr_q = (double) (consecutive_acq * 1000.0 / duration);
  double consecutive_acq_perc = (1-(thr - thr_q)/thr) * 100;
  printf("#consequtive  : %10lu ( %10.0f / s) = %.2f%%\n", consecutive_acq, thr_q, consecutive_acq_perc);
#endif
  rapl_stats_t s;
  RR_STATS(&s);

  double ppw0 = thr / s.power_total[NUMBER_OF_SOCKETS];
  double ppw1 = thr / s.power_package[NUMBER_OF_SOCKETS];
  double ppw2 = thr / s.power_pp0[NUMBER_OF_SOCKETS];
  double eop0 = (1e6 * s.energy_total[NUMBER_OF_SOCKETS]) / acquires;
  double eop1 = (1e6 * s.energy_package[NUMBER_OF_SOCKETS]) / acquires;
  double eop2 = (1e6 * s.energy_pp0[NUMBER_OF_SOCKETS]) / acquires;
  printf("#ppw (ops/W)  : %10.1f | %10.1f | %10.1f\n", ppw0, ppw1, ppw2);
  printf("#eop (uJ/op)  : %10f | %10f | %10f\n", eop0, eop1, eop2);

  cdf_t* cdf_lock = cdf_calc((size_t*) vals_lock, vals_idx);
  cdf_t* cdf_unlock = cdf_calc((size_t*) vals_unlock, vals_idx);

  //  cdf_print(cdf_lock);
  printf("#lock\n");
  double limits[CDF_BOXPLOT_VALS] = {0, 100 - test_boxplot_perc, 25, 50, 75, test_boxplot_perc, 99.99};
  cdf_print_boxplot_limits(cdf_lock, limits, "lock");
  printf("#unlock\n");
  cdf_print_boxplot_limits(cdf_unlock, limits, "unlock");

  free(vals_lock);
  free(vals_unlock);

  cdf_destroy(cdf_lock);
  cdf_destroy(cdf_unlock);

  free(locks);
  free(threads);
  free(data);

  return 0;
}

