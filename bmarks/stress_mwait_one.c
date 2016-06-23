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

#define TAS_MWAIT_LAT 1

#include "rapl_read.h"
#include "tas_mwait_in.h"

#define DELAY_NONE      0
#define DELAY_NORMAL    1
#define DELAY_FAIR      2
#define DELAY_FAIR_PLUS 3

#define DELAY_FOR_FAIRNESS 0

#define DELAY         DELAY_FAIR

#define MWAIT_MEM_SIZE (1024*1024LL)

#define STR(s) #s
#define XSTR(s) STR(s)

//number of concurres threads
#define DEFAULT_NUM_THREADS 1
//whether or not to set cpu
#define DEFAULT_SET_CPU 1
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
int fp;

__thread uint32_t phys_id;

typedef struct shared_data
{
  volatile char the_data[CACHE_LINE_SIZE];
} shared_data;


tas_mwait_lock_t* locks;
__attribute__((aligned(CACHE_LINE_SIZE))) volatile shared_data* protected_data;
__attribute__((aligned(CACHE_LINE_SIZE))) volatile uint32_t* protected_offsets;
int duration;
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
      ticks s_lat;
      ticks n_lat;
      int id;
    };
    char padding[CACHE_LINE_SIZE];
  };
} thread_data_t;


#if DELAY == DELAY_FAIR_PLUS
size_t shared_counter = 0;
#endif

#define TAS_MWAIT_FREE   0
#define TAS_MWAIT_LOCKED 1

#define CACHE_LINE_SIZE 64
#if !defined(PAUSE_IN)
#  define PAUSE_IN()			\
  ;
#endif

uint8_t non_set_cpu_map[] =		
  {
    0, 10, 1, 11, 2, 12, 3, 13, 4, 14,
    5, 15, 6, 16, 7, 17, 8, 18, 9, 19,
    20, 30, 21, 31, 22, 32, 23, 33, 24, 34,
    25, 35, 26, 26, 27, 37, 28, 38, 29, 39
  };

void*
test(void *data)
{
  thread_data_t *d = (thread_data_t *)data;
  phys_id = the_cores[d->id];
  if (do_set_cpu)
    {
      set_cpu(phys_id);
    }
  else
    {
      set_cpu(non_set_cpu_map[d->id]);
    }

#if DELAY == DELAY_FAIR_PLUS
  size_t counter_prev = -1;
  size_t num_consecutive_acq = 0;
#endif

  UNUSED size_t sum = 0;
  size_t num_acquires = 0;

  /* Wait on barrier */
  cpause(1000000000);
  barrier_cross(d->barrier);

  while (stop == 0) 
    {
      ticks lat = 0;
      tas_mwait_lock_lock(locks, fp, 0, &lat);      
#if TAS_MWAIT_LAT == 1
      if (lat)
      {
	d->s_lat += lat;
	d->n_lat++;
      }
#endif

#if DELAY == DELAY_FAIR_PLUS
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
	      protected_data[i + protected_offsets[0]].the_data[0] += d->id;
	    } 
	  else 
	    {
	      sum += protected_data[i + protected_offsets[0]].the_data[0];
	    }
        }
#endif

      tas_mwait_lock_unlock(locks);      

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

#if DELAY >= DELAY_FAIR
      if (d->fair_delay)
	{
	  cpause(d->fair_delay);
	}
#endif

      num_acquires++;
    }

  d->num_acquires = num_acquires;
#if DELAY == DELAY_FAIR_PLUS
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
      {"duration",                  required_argument, NULL, 'd'},
      {"num-threads",               required_argument, NULL, 'n'},
      {"set-cpu",                   required_argument, NULL, 's'},
      {"acquire",                   required_argument, NULL, 'a'},
      {"fair",                      required_argument, NULL, 'f'},
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
      c = getopt_long(argc, argv, "hvd:n:s:w:a:p:c:o:f:", long_options, &i);

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
  assert(duration >= 0);
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
      protected_data = (shared_data*) calloc(cl_access, sizeof(shared_data));
      protected_offsets = (uint32_t*) calloc(1, sizeof(shared_data));
      protected_offsets[0]= cl_access * 0;
    }

  fp = open("/dev/myDevice", O_RDWR);
  if (fp < 0)
    {
      printf("Error: open\n");
      return -1;
    }

  char* address = mmap(NULL, MWAIT_MEM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fp, 0);

  if (address == MAP_FAILED)
    {
      printf("Error: mmap\n");
      return -1;
    }


  locks = (tas_mwait_lock_t*) address;
  assert(locks != NULL);

  tas_mwait_lock_init(locks, NULL);
  

  if (verbose)
    {
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

  cpause(1000000000);

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
  size_t max_acq = 0, min_acq = -1;
  size_t s_lat = 0, n_lat = 0;
  for (i = 0; i < num_threads; i++) 
    {
      if (verbose)
	{
	  printf("Thread: %3d : #acquire   : %lu\n", i, data[i].num_acquires);
#if DELAY == DELAY_FAIR_PLUS
	  printf("              #consequ   : %lu\n", data[i].num_consecutive_acq);
#endif
	}
      size_t num_acq = data[i].num_acquires;
      acquires += num_acq;
      consecutive_acq += data[i].num_consecutive_acq;
      s_lat += data[i].s_lat;
      n_lat += data[i].n_lat;
      if (num_acq > max_acq)
	{
	  max_acq = num_acq;
	}
      if (num_acq < min_acq)
	{
	  min_acq = num_acq;
	}
    }

  if (verbose)
    {
      printf("Duration      : %d (ms)\n", duration);
    }

  RR_PRINT_UNPROTECTED(RAPL_PRINT_ENE);

  double thr = (double) (acquires * 1000.0 / duration);
  printf("#acquires     : %10lu ( %10.0f / s)\n", acquires, thr);
#if DELAY == DELAY_FAIR_PLUS
  double thr_q = (double) (consecutive_acq * 1000.0 / duration);
  double consecutive_acq_perc = (1-(thr - thr_q)/thr) * 100;
  printf("#consequtive  : %10lu ( %10.0f / s) = %.2f%%\n", consecutive_acq, thr_q, consecutive_acq_perc);

  double fair_ratio = (double) max_acq / min_acq;
  printf("#ratio max/min: %.2f\n", fair_ratio);
#endif

#if TAS_MWAIT_LAT == 1
  size_t a_lat = (n_lat) ? s_lat / n_lat : 0;
  printf("#avg mwait lat: %zu (%zu times)\n", a_lat, n_lat);
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

  /* free(locks); */
  free(threads);
  free(data);
  close(fp);
  return 0;
}
