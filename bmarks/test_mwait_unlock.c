#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/futex.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <fcntl.h>
#include <time.h>

#define TAS_MWAIT_LAT 1

#include "utils.h"
#include "tas_mwait_in.h"

#define DELAY_NONE    0
#define DELAY_NORMAL  1
#define DELAY_FAIR    2

#define DELAY_FOR_FAIRNESS 0

#define DELAY         DELAY_NORMAL

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
#define DEFAULT_MWAIT_VAL 1
//delay between lock acquire and release in cycles
#define DEFAULT_CL_ACCESS 0
//the total duration of a test
#define DEFAULT_DURATION 1000
//if do_writes is 0, the test only reads cache lines, else it also writes them
#define DEFAULT_DO_WRITES 0
//which level of prints to do with RAPL
#define DEFAULT_VERBOSE 0

static volatile int stop = 0;
int fp;

__thread uint32_t phys_id;

typedef struct shared_data
{
  volatile char the_data[64];
} shared_data;

__attribute__((aligned(CACHE_LINE_SIZE))) volatile shared_data* protected_data;
__attribute__((aligned(CACHE_LINE_SIZE))) volatile uint32_t* protected_offsets;
int duration;
int num_locks;
int do_set_cpu;
int do_writes;
int num_threads;
int acq_duration;
int acq_delay;
int mwait_val;
int mutex_delay;
int cl_access;
int power_print;
int verbose;

__thread volatile size_t sense_local = 0;

typedef struct barrier
{
  size_t thread_n;
  volatile size_t crossed;
  volatile size_t sense_global;
} barrier_t;

void barrier_init(barrier_t* b, int n)
{
  b->thread_n = n;
  b->crossed = n;
  b->sense_global = 1;
}

void barrier_cross(barrier_t *b)
{
  sense_local = !b->sense_global;
  if (__sync_fetch_and_sub(&b->crossed, 1) == 1)
    {
      //      printf("------> got 1 \n");
      b->crossed = b->thread_n;
      b->sense_global = sense_local;
    }
  else
    {
      while (sense_local != b->sense_global && !stop)
	{
	  asm volatile ("mfence");
	}
    }
  sense_local = !sense_local;
}

#define CACHE_LINE_SIZE 64

typedef tas_mwait_lock_t _lock_t;

_lock_t* lock;

typedef struct thread_data 
{
  union
  {
    struct
    {
      barrier_t* barrier;
      barrier_t* barrier_thr;
      unsigned long num_acquires;
      unsigned long num_consecutive_acq;
      unsigned long fair_delay;
      size_t n_lock;
      size_t n_unlock;
      size_t n_cdelay;
      size_t ticks_lock;
      size_t ticks_unlock;
      size_t ticks_cdelay;
      int id;
    };
    char padding[2*CACHE_LINE_SIZE];
  };
} thread_data_t;

static void
_lock(_lock_t* m, thread_data_t* d)
{
  ticks lat = 0;
  volatile ticks __lat_s = getticks();
  UNUSED int r = read(fp, NULL, 0);
  volatile ticks __lat_e = getticks(); 
  lat = __lat_e - __lat_s;
  if (lat)
    {
      d->n_lock++;
      d->ticks_lock += lat;
    }
}

static void
_unlock(_lock_t* m, thread_data_t* d)
{
  volatile ticks s = getticks();
  tas_mwait_lock_unlock(m);
  _mm_mfence();
  volatile ticks e = getticks();
  d->n_unlock++;
  d->ticks_unlock += (e - s);
}

volatile size_t num_woken = 0;

void*
test(void *data)
{
  thread_data_t *d = (thread_data_t *)data;
  phys_id = the_cores[d->id];
  if (do_set_cpu)
    {
      set_cpu(phys_id);
    }

  /* Wait on barrier */
  barrier_cross(d->barrier);

  while (!stop)
    {
      barrier_cross(d->barrier_thr);
      lock->lock = mwait_val;
      barrier_cross(d->barrier_thr);
      if (d->id == 0)
      	{
      	  if (acq_duration)
      	    {
      	      volatile ticks s = getticks();
      	      cdelay(acq_duration);
      	      volatile ticks e = getticks();
      	      d->n_cdelay++;
      	      d->ticks_cdelay += (e - s);
      	      asm volatile ("mfence");
      	    }

      	  _unlock(lock, d);
      	  d->num_acquires++;
      	}
      else
      	{
      	  _lock(lock, d);
      	  d->num_acquires++;
      	}

    }

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
  barrier_t barrier_thr;
  struct timeval start, end;
  struct timespec timeout;
  duration = DEFAULT_DURATION;
  num_locks = DEFAULT_NUM_LOCKS;
  do_set_cpu = DEFAULT_SET_CPU;
  do_writes = DEFAULT_DO_WRITES;
  num_threads = DEFAULT_NUM_THREADS;
  acq_duration = DEFAULT_ACQ_DURATION;
  mwait_val = DEFAULT_MWAIT_VAL;
  acq_delay = DEFAULT_ACQ_DELAY;
  cl_access = DEFAULT_CL_ACCESS;
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
		 "  -f, --free <int>\n"
		 "        Number of pauses after a lock is released (to ensure fairness) (default=" XSTR(DEFAULT_FREE) ")\n"
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
	  acq_duration = atoi(optarg);
	  break;
	case 'p':
	  acq_delay = atoi(optarg);
	  break;
	case 'c':
	  cl_access = atoi(optarg);
	  break;
	case 'f':
	  mwait_val = atoi(optarg);
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

  if (verbose)
    {
      printf("Number of locks        : %d\n", num_locks);
      printf("Duration               : %d\n", duration);
      printf("Number of threads      : %d\n", num_threads);
      printf("Lock is held for       : %d\n", acq_duration);
      printf("Delay between locks    : %d\n", acq_delay);
      printf("Cache lines accessed   : %d\n", cl_access);
      printf("Do writes              : %d\n", do_writes);
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

  fp = open("/dev/myDevice", O_RDWR);
  if (fp < 0)
    {
      printf("Error: open\n");
      return -1;
    }

  char* address = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fp, 0);

  if (address == MAP_FAILED)
    {
      printf("Error: mmap\n");
      return -1;
    }


  lock = (tas_mwait_lock_t*) address;
  assert(lock != NULL);


  if(duration > 0)
    {
      stop = 0;
    }

  /* Access set from all threads */
  barrier_init(&barrier, num_threads + 1);
  barrier_init(&barrier_thr, num_threads);
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  for (i = 0; i < num_threads; i++) 
    {
      data[i].id = i;
      data[i].num_acquires = 0;
      data[i].num_consecutive_acq = 0;
      data[i].barrier = &barrier;
      data[i].barrier_thr = &barrier_thr;
      data[i].n_cdelay = 0;
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


  /* Start threads */
  barrier_cross(&barrier);

  gettimeofday(&start, NULL);

  if (duration > 0) 
    {
      nanosleep(&timeout, NULL);
    } 
  stop = 1;


  gettimeofday(&end, NULL);
  /* Wait for thread completion */
  /* for (i = 0; i < num_threads; i++)  */
  /*   { */
  /*     if (pthread_join(threads[i], NULL) != 0)  */
  /* 	{ */
  /* 	  fprintf(stderr, "Error waiting for thread completion\n"); */
  /* 	  exit(1); */
  /* 	} */
  /*   } */

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
  size_t n_lock = 0, n_unlock = 0, n_cdelay = 0, ticks_lock = 0, ticks_unlock = 0, ticks_cdelay = 0;
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
      n_lock += data[i].n_lock;
      n_unlock += data[i].n_unlock;
      n_cdelay += data[i].n_cdelay;
      ticks_lock += data[i].ticks_lock;
      ticks_unlock += data[i].ticks_unlock;
      ticks_cdelay += data[i].ticks_cdelay;
    }

  stop = 1;
  lock->lock = 0;

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
      printf("Duration      : %d (ms)\n", duration);
    }

  
  double thr = (double) (acquires * 1000.0 / duration);
  printf("#acquires     : %10lu ( %10.0f / s)\n", acquires, thr);
#if DELAY == DELAY_FAIR
  double thr_q = (double) (consecutive_acq * 1000.0 / duration);
  double consecutive_acq_perc = (1-(thr - thr_q)/thr) * 100;
  printf("#consequtive  : %10lu ( %10.0f / s) = %.2f%%\n", consecutive_acq, thr_q, consecutive_acq_perc);
#endif

  n_lock = (!n_lock) ? 1 : n_lock;
  printf("#avg sleep lo : %-10zu (%zu times)\n", ticks_lock / n_lock, n_lock);
  printf("#avg sleep un : %-10zu (%zu times)\n", ticks_unlock / n_unlock, n_unlock);
  n_cdelay = (!n_cdelay) ? 1 : n_cdelay;
  printf("#avg cdelay   : %-10zu (%zu times)\n", ticks_cdelay / n_cdelay, n_cdelay);
  
  free(threads);
  free(data);
  close(fp);

  return 0;
}
 
