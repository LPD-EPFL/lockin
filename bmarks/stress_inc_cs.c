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

#include "rapl_read.h"
#include "lock_in.h"

#define DELAY_NONE    0
#define DELAY_NORMAL  1
#define DELAY_FAIR    2

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
#define DEFAULT_NUM_ACQ 10000
//delay between consecutive acquire attempts in cycles
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
int acq_duration_inc = 1;
size_t acq_duration_max = -1;
int acq_delay;
int fair_delay;
int mutex_delay;
int cl_access;
int power_print;
int verbose;

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


typedef struct thread_data {
  union
  {
    struct
    {
      barrier_t *barrier;
      unsigned long num_acquires;
      unsigned long num_consecutive_acq;
      unsigned long fair_delay;
      int id;
    };
    char padding[CACHE_LINE_SIZE];
  };
} thread_data_t;


#if DELAY == DELAY_FAIR
size_t shared_counter = 0;
#endif

void*
test(void *data)
{
  thread_data_t *d = (thread_data_t *)data;
  int rand_max = num_locks - 1;
  phys_id = the_cores[d->id];
  if (do_set_cpu)
    {
      set_cpu(phys_id);
    }

  seeds = seed_rand();
#if DELAY == DELAY_FAIR
  size_t counter_prev = -1;
  size_t num_consecutive_acq = 0;
#endif


  UNUSED size_t sum = 0;
  size_t num_acquires = 0;

  /* Wait on barrier */
  barrier_cross(d->barrier);

  while (stop == 0) 
    {
      int lock_to_acq = (int) my_random(&(seeds[0]),&(seeds[1]),&(seeds[2])) & rand_max;

      pthread_mutex_lock(locks + lock_to_acq);      

#if DELAY == DELAY_FAIR
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
      if (acq_duration < acq_duration_max)
	{
	  acq_duration += acq_duration_inc;
	}

      if (verbose && (acq_duration & 2047) == 0)
	{
	  printf("# CS dur: %d\n", acq_duration);
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

      pthread_mutex_unlock(locks + lock_to_acq);      

      /* if (num_threads > 2) */
      /* 	{ */
      /* 	  pause_rep(32); */
      /* 	} */

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
#if DELAY == DELAY_FAIR
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
      {"inc",                       required_argument, NULL, 'i'},
      {"max",                       required_argument, NULL, 'm'},
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

  /* sigset_t block_set; */

  while(1) 
    {
      i = 0;
      c = getopt_long(argc, argv, "hvd:n:s:l:w:a:p:c:o:f:i:m:", long_options, &i);

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
		 "  -l, --lcoks <int>\n"
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
		 "  -p, --power <int>\n"
		 "        Print output level for RAPL power measurements (default=" XSTR(DEFAULT_POW_PRINT) ")\n"
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
	case 'i':
	  acq_duration_inc = atoi(optarg);
	  break;
	case 'm':
	  acq_duration_max = atoi(optarg);
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

  if (do_set_cpu)
    {
      set_cpu(the_cores[0]);
    }

  if (cl_access > 0)
    {
      protected_data = (shared_data*) calloc(cl_access * num_locks, sizeof(shared_data));
      protected_offsets = (uint32_t*) calloc(num_locks, sizeof(shared_data));
      int j;
      for (j = 0; j < num_locks; j++) {
	protected_offsets[j]=cl_access * j;
      }
    }

  locks = (pthread_mutex_t*) malloc(num_locks * sizeof(pthread_mutex_t));
  assert(locks != NULL);

  int l;
  for (l = 0; l < num_locks; l++)
    {
      pthread_mutex_init(locks + l, NULL);
    }
  

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
    }

  if (verbose)
    {
      printf("Duration      : %d (ms)\n", duration);
    }

  RR_PRINT_UNPROTECTED(RAPL_PRINT_ENE);

  double thr = (double) (acquires * 1000.0 / duration);
  printf("#acquires     : %10lu ( %10.0f / s)\n", acquires, thr);
#if DELAY == DELAY_FAIR
  double thr_q = (double) (consecutive_acq * 1000.0 / duration);
  double consecutive_acq_perc = (1-(thr - thr_q)/thr) * 100;
  printf("#consequtive  : %10lu ( %10.0f / s) = %.2f%%\n", consecutive_acq, thr_q, consecutive_acq_perc);
#endif
  rapl_stats_t s;
  RR_STATS(&s);

  double ppw0 = thr / s.power_package[NUMBER_OF_SOCKETS];
  double ppw1 = thr / s.power_pp0[NUMBER_OF_SOCKETS];
  double ppw2 = thr / s.power_rest[NUMBER_OF_SOCKETS];
  double eop0 = (1e6 * s.energy_package[NUMBER_OF_SOCKETS]) / acquires;
  double eop1 = (1e6 * s.energy_pp0[NUMBER_OF_SOCKETS]) / acquires;
  double eop2 = (1e6 * s.energy_rest[NUMBER_OF_SOCKETS]) / acquires;
  printf("#ppw (ops/W)  : %10.1f | %10.1f | %10.1f\n", ppw0, ppw1, ppw2);
  printf("#eop (uJ/op)  : %10f | %10f | %10f\n", eop0, eop1, eop2);

  free(locks);
  free(threads);
  free(data);

  return 0;
}
