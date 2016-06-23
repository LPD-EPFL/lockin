#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#ifndef __sparc__
#include <numa.h>
#endif
#include "gl_lock.h"
#include "utils.h"
#include "lock_if.h"
#include "atomic_ops.h"

/* #define PRINT_OUTPUT */
/* #define LIKWID_PERFMON */

#include "rapl_read.h"
#include <likwid.h>

#define STR(s) #s
#define XSTR(s) STR(s)

//number of concurres threads
#define DEFAULT_NUM_THREADS 1
//total number of locks
#define DEFAULT_NUM_LOCKS 1
//number of lock acquisitions in this test
#define DEFAULT_NUM_ACQ 10000
//delay between consecutive acquire attempts in cycles
#define DEFAULT_ACQ_DELAY 0
//delay between lock acquire and release in cycles
#define DEFAULT_ACQ_DURATION 0
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
__thread uint32_t cluster_id;
volatile global_data the_locks;
__attribute__((aligned(CACHE_LINE_SIZE))) volatile local_data* local_th_data;
__attribute__((aligned(CACHE_LINE_SIZE))) volatile uint32_t* protected_offsets;
int duration;
int num_locks;
int num_threads;
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
            int id;
        };
        char padding[CACHE_LINE_SIZE];
    };
} thread_data_t;

void*
test(void *data)
{
  thread_data_t *d = (thread_data_t *)data;
  phys_id = the_cores[d->id];
  cluster_id = get_cluster(phys_id);

  LIKWID_MARKER_THREADINIT;
    
  /* local initialization of locks */
  local_th_data[d->id] = init_lock_array_local(phys_id, num_locks, the_locks);

  /* Wait on barrier */
  barrier_cross(d->barrier);

  int lock_to_acq;

  local_data local_d = local_th_data[d->id];

  LIKWID_MARKER_START("Compute");

  if (num_locks == 1) 
    {
      lock_to_acq = 0;
    } 
  else 
    {
      lock_to_acq = d->id % num_locks;
    }

  while (stop == 0) 
    {
      acquire_lock(&local_d[lock_to_acq],&the_locks[lock_to_acq]);
      d->num_acquires++;
    }

  LIKWID_MARKER_STOP("Compute");
  LIKWID_MARKER_START("postprocess");
  LIKWID_MARKER_STOP("postprocess");

  return NULL;
}


struct timeval start, end;
struct timespec timeout;

void*
power(void *data)
{
  thread_data_t *d = (thread_data_t *)data;
  phys_id = the_cores[d->id];
  set_cpu(phys_id);

  RR_INIT(phys_id);
  /* Start threads */
  barrier_cross(d->barrier);
  gettimeofday(&start, NULL);

  RR_START_SIMPLE();
  if (duration > 0)
    {
      nanosleep(&timeout, NULL);
    } 
  RR_STOP_SIMPLE();
  barrier_cross(d->barrier);
  RR_TERM();

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
  int core = the_cores[0];
  set_cpu(core);
  struct option long_options[] = 
    {
      // These options don't set a flag
      {"help",                      no_argument,       NULL, 'h'},
      {"verbose",                   no_argument,       NULL, 'v'},
      {"locks",                     required_argument, NULL, 'l'},
      {"duration",                  required_argument, NULL, 'd'},
      {"num-threads",               required_argument, NULL, 'n'},
      {"acquire",                   required_argument, NULL, 'a'},
      {"pause",                     required_argument, NULL, 'p'},
      {"do_writes",                 required_argument, NULL, 'w'},
      {"clines",                    required_argument, NULL, 'c'},
      {"power",                     required_argument, NULL, 'o'},
      {NULL, 0, NULL, 0}
    };

  int i, c;
  thread_data_t *data;
  pthread_t *threads;
  pthread_attr_t attr;
  barrier_t barrier;
  duration = DEFAULT_DURATION;
  num_locks = DEFAULT_NUM_LOCKS;
  num_threads = DEFAULT_NUM_THREADS;
  power_print = DEFAULT_POW_PRINT;
  verbose = DEFAULT_VERBOSE;

  /* sigset_t block_set; */

  while(1) 
    {
      i = 0;
      c = getopt_long(argc, argv, "hvl:d:n:w:a:p:c:o:", long_options, &i);

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
		 "  -a, --acquire <int>\n"
		 "        Number of cycles a lock is held (default=" XSTR(DEFAULT_ACQ_DURATION) ")\n"
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
  num_locks=pow2roundup(num_locks);
  assert(duration >= 0);
  assert(num_locks >= 1);
  assert(num_threads > 0);

#ifdef PRINT_OUTPUT
  printf("Number of locks        : %d\n", num_locks);
  printf("Duration               : %d\n", duration);
  printf("Number of threads      : %d\n", num_threads);
#endif
  timeout.tv_sec = duration / 1000;
  timeout.tv_nsec = (duration % 1000) * 1000000;

  LIKWID_MARKER_INIT;

  if ((data = (thread_data_t *)malloc((num_threads + 1) * sizeof(thread_data_t))) == NULL) 
    {
      perror("malloc");
      exit(1);
    }
  if ((threads = (pthread_t *)malloc((num_threads + 1) * sizeof(pthread_t))) == NULL) 
    {
      perror("malloc");
      exit(1);
    }

  local_th_data = (local_data *)malloc(num_threads * sizeof(local_data));

  if(duration > 0)
    {
      stop = 0;
    }

  /* Init locks */
  the_locks = init_lock_array_global(num_locks, num_threads);

  /* Access set from all threads */
  barrier_init(&barrier, num_threads + 1);
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  for (i = 0; i < num_threads; i++) 
    {
      data[i].id = i;
      data[i].num_acquires = 0;
      data[i].barrier = &barrier;
      if (pthread_create(&threads[i], &attr, test, (void *)(&data[i])) != 0)
	{
	  fprintf(stderr, "Error creating thread\n");
	  exit(1);
	}
    }

  /* int core_other_socket = 0; */
  /* int this_socket = get_cluster(core); */
  /* while (get_cluster(the_cores[core_other_socket]) == this_socket) */
  /*   { */
  /*     core_other_socket++; */
  /*   } */

  /* if (core_other_socket < num_threads) */
  /*   { */
  /*     barrier_init(&barrier_power, 2); */
  /*     data[i].id = core_other_socket; */
  /*     data[i].barrier = &barrier_power; */

  /*     if (pthread_create(&threads[i], &attr, power, (void *)(&data[i])) != 0) */
  /* 	{ */
  /* 	  fprintf(stderr, "Error creating thread\n"); */
  /* 	  exit(1); */
  /* 	} */
  /*   } */
  /* else */
  /*   { */
  /*     barrier_init(&barrier_power, 1); */
  /*   } */

  pthread_attr_destroy(&attr);
  /* Catch some signals */
  if (signal(SIGHUP, catcher) == SIG_ERR ||
      signal(SIGINT, catcher) == SIG_ERR ||
      signal(SIGTERM, catcher) == SIG_ERR) {
    perror("signal");
    exit(1);
  }

#if NUMBER_OF_SOCKETS == 1
  RR_INIT(core);
#else
  RR_INIT_ALL();
#endif
  /* Start threads */
  /* barrier_cross(&barrier_power); */
  barrier_cross(&barrier);

  gettimeofday(&start, NULL);

#if NUMBER_OF_SOCKETS == 1
  RR_START_SIMPLE();
#else
  RR_START_UNPROTECTED_ALL();
#endif
  if (duration > 0)
    {
      nanosleep(&timeout, NULL);
    } 
#if NUMBER_OF_SOCKETS == 1
  RR_STOP_SIMPLE();
#else
  RR_STOP_UNPROTECTED_ALL();
#endif
  RR_TERM();


  //  barrier_cross(&barrier_power);
  //  RR_PRINT(power_print);
  RR_PRINT_UNPROTECTED(power_print);

  duration = (end.tv_sec * 1000 + end.tv_usec / 1000) - (start.tv_sec * 1000 + start.tv_usec / 1000);

  if (verbose)
    {
      printf("Duration      : %d (ms)\n", duration);
    }

  double thr = 0;
  printf("#acquires     : %10lu ( %10.0f / s)\n", 0L, thr);
  rapl_stats_t s;
  RR_STATS(&s);
  double ppw0 = 0;
  double ppw1 = 0;
  double ppw2 = 0;
  double eop0 = 0;
  double eop1 = 0;
  double eop2 = 0;
  printf("#ppw (ops/W)  : %10.1f | %10.1f | %10.1f\n", ppw0, ppw1, ppw2);
  printf("#eop (J/op)   : %10f | %10f | %10f\n", eop0, eop1, eop2);

  LIKWID_MARKER_CLOSE;
  return 0;
}
