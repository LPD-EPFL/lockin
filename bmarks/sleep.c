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

/* #define PRINT_OUTPUT */
/* #define LIKWID_PERFMON */

#include "rapl_read.h"

#define STR(s) #s
#define XSTR(s) STR(s)

//number of concurres threads
#define DEFAULT_NUM_THREADS 1
//number of lock acquisitions in this test
#define DEFAULT_NUM_ACQ 10000
//delay between consecutive acquire attempts in cycles
#define DEFAULT_ACQ_DELAY 0
//delay between lock acquire and release in cycles
#define DEFAULT_ACQ_DURATION 0
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

__thread uint32_t phys_id;
__thread uint32_t cluster_id;


volatile uint8_t bytes[CACHE_LINE_SIZE] = {1, 1, 1, 1, 1, 1, 1, 1,
					    1, 1, 1, 1, 1, 1, 1, 1, 
					    1, 1, 1, 1, 1, 1, 1, 1, 
					    1, 1, 1, 1, 1, 1, 1, 1, 
					    1, 1, 1, 1, 1, 1, 1, 1, 
					    1, 1, 1, 1, 1, 1, 1, 1, 
					    1, 1, 1, 1, 1, 1, 1, 1, 
					    1, 1, 1, 1, 1, 1, 1, 1,};

int duration;
int num_threads;
int power_print;
int verbose;

typedef struct barrier {
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
  set_cpu(phys_id);

  size_t us = 1000 * duration;

  RR_INIT(phys_id);
  /* Wait on barrier */


  barrier_cross(d->barrier);
  
  RR_START_SIMPLE();

  usleep(us);

  RR_STOP_SIMPLE();
  RR_TERM();

  d->num_acquires = 1;

  RR_PRINT(power_print);

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
  set_cpu(the_cores[3]);
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
  struct timeval start, end;
  struct timespec timeout;
  duration = DEFAULT_DURATION;
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
		 "  -d, --duration <int>\n"
		 "        Test duration in milliseconds (0=infinite, default=" XSTR(DEFAULT_DURATION) ")\n"
		 "  -n, --num-threads <int>\n"
		 "        Number of threads (default=" XSTR(DEFAULT_NUM_THREADS) ")\n"
		 "  -p, --power <int>\n"
		 "        Print output level for RAPL power measurements (default=" XSTR(DEFAULT_POW_PRINT) ")\n"
		 );
	  exit(0);
	case 'v':
	  verbose = 1;
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
  assert(duration >= 0);
  assert(num_threads > 0);
#if defined(PRINT_OUTPUT)
      printf("Duration               : %d\n", duration);
      printf("Number of threads      : %d\n", num_threads);
    }
#endif
  timeout.tv_sec = duration / 1000;
  timeout.tv_nsec = (duration % 1000) * 1000000;

  if ((data = (thread_data_t *)malloc(num_threads * sizeof(thread_data_t))) == NULL) 
    {
      perror("malloc");
      exit(1);
    }
  if ((threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t))) == NULL) 
    {
      perror("malloc");
      exit(1);
    }

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
    data[i].barrier = &barrier;
    if (pthread_create(&threads[i], &attr, test, (void *)(&data[i])) != 0) {
      fprintf(stderr, "Error creating thread\n");
      exit(1);
    }
  }
  pthread_attr_destroy(&attr);

  /* Catch some signals */
  if (signal(SIGHUP, catcher) == SIG_ERR ||
      signal(SIGINT, catcher) == SIG_ERR ||
      signal(SIGTERM, catcher) == SIG_ERR) {
    perror("signal");
    exit(1);
  }

  /* Start threads */
  barrier_cross(&barrier);
  gettimeofday(&start, NULL);
  if (duration > 0) {
    nanosleep(&timeout, NULL);
  } 
  /* else { */
  /*         sigemptyset(&block_set); */
  /*         sigsuspend(&block_set); */
  /*     } */
  stop = 1;
  gettimeofday(&end, NULL);
  /* Wait for thread completion */
  for (i = 0; i < num_threads; i++) {
    if (pthread_join(threads[i], NULL) != 0) {
      fprintf(stderr, "Error waiting for thread completion\n");
      exit(1);
    }
  }

  /* duration = (end.tv_sec * 1000 + end.tv_usec / 1000) - (start.tv_sec * 1000 + start.tv_usec / 1000); */

  unsigned long acquires = 0;
  for (i = 0; i < num_threads; i++) 
    {
      if (verbose)
	{
	  printf("Thread: %3d : #acquire   : %lu\n", i, data[i].num_acquires);
	}
      acquires += data[i].num_acquires;
    }

  /* Cleanup locks */
  free(threads);
  free(data);

  return 0;
}

