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
//the total duration of a test
#define DEFAULT_DURATION 1000
//if do_writes is 0, the test only reads cache lines, else it also writes them
#define DEFAULT_DO_WRITES 0
//which level of prints to do with RAPL
#define DEFAULT_POW_PRINT RAPL_PRINT_ENE
#define DEFAULT_VERBOSE 0

static volatile int stop = 0;

pthread_mutex_t* locks;
int duration;
int num_locks;
int do_set_cpu;
int num_threads;
int power_down_other_cores = 0;
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
  int phys_id = the_cores[d->id];
  if (do_set_cpu)
    {
      set_cpu(phys_id);
    }

#if LOCK_IN == TICKETDVFS
  dvfs_freq_init(phys_id);
#endif

  /* Wait on barrier */
  barrier_cross(d->barrier);

  int lock_to_acq;

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
      pthread_mutex_lock(locks + lock_to_acq);
      d->num_acquires++;
    }


  return NULL;
}


struct timeval start, end;
struct timespec timeout;

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
      {"pause",                     required_argument, NULL, 'p'},
      {"do_writes",                 required_argument, NULL, 'w'},
      {"clines",                    required_argument, NULL, 'c'},
      {"power_others",              no_argument,       NULL, 'o'},
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
  num_threads = DEFAULT_NUM_THREADS;
  verbose = DEFAULT_VERBOSE;

  /* sigset_t block_set; */

  while(1) 
    {
      i = 0;
      c = getopt_long(argc, argv, "hvd:n:s:l:w:a:p:c:of:", long_options, &i);

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
	case 'o':
	  power_down_other_cores = 1;
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

  locks = (pthread_mutex_t*) malloc(num_locks * sizeof(pthread_mutex_t));
  assert(locks != NULL);

  int l;
  for (l = 0; l < num_locks; l++)
    {
      pthread_mutex_init(locks + l, NULL);
    }
  

#if LOCK_IN == TICKETDVFS
  if (power_down_other_cores)
    {
      dvfs_freq_set_range(num_threads, 0, DVFS_FREQ_MIN);
    }
#endif

  if (verbose)
    {
      printf("Number of locks        : %d\n", num_locks);
      printf("Duration               : %d\n", duration);
      printf("Number of threads      : %d\n", num_threads);
      printf("sizeof(lock)           : %zu\n", sizeof(pthread_mutex_t));
    }
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

  duration = (end.tv_sec * 1000 + end.tv_usec / 1000) - (start.tv_sec * 1000 + start.tv_usec / 1000);

  if (verbose)
    {
      printf("Duration      : %d (ms)\n", duration);
    }

  RR_PRINT_UNPROTECTED(RAPL_PRINT_POW);

  double thr = 0;
  printf("#acquires     : %10lu ( %10.0f / s)\n", 0L, thr);
  double ppw0 = 0;
  double ppw1 = 0;
  double ppw2 = 0;
  double eop0 = 0;
  double eop1 = 0;
  double eop2 = 0;
  printf("#ppw (ops/W)  : %10.1f | %10.1f | %10.1f\n", ppw0, ppw1, ppw2);
  printf("#eop (uJ/op)  : %10f | %10f | %10f\n", eop0, eop1, eop2);

#if LOCK_IN == TICKETDVFS
  dvfs_freq_set_all_max();
#endif
  free(locks);
  free(threads);
  free(data);

  return 0;
}
