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
/* #include "gl_lock.h" */
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
#define DEFAULT_VERBOSE   0
#define DEFAULT_SCENARIO  0

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
int scenario;

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

typedef struct thread_data 
{
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
  volatile thread_data_t *d = (thread_data_t *)data;
  phys_id = the_cores[d->id];
  cluster_id = get_cluster(phys_id);
  set_cpu(phys_id);

  RR_INIT(phys_id);
  /* Wait on barrier */

  register size_t na = 1024;

  while (na-- > 0)
    {
      na += stop;
    }


  switch (scenario)
    {
    case 0:
      barrier_cross(d->barrier);
  
      RR_START_SIMPLE();
      while(stop == 0)
	{
	  na++;
	}

      RR_STOP_SIMPLE();
      break;
    case 12:
      barrier_cross(d->barrier);
      volatile size_t a = 0;
      RR_START_SIMPLE();
      while(a++ < 3.5e9);
      RR_STOP_SIMPLE();
      na = a;
      break;
    case 13:
      {
	barrier_cross(d->barrier);
	volatile size_t a = 0;
	RR_START_SIMPLE();
	while(stop == 0)
	  {
	    a++;
	  }
	RR_STOP_SIMPLE();
	na = a;
      }
      break;
    case 14:
      {
	barrier_cross(d->barrier);
	volatile size_t a = 0;
	RR_START_SIMPLE();
	while(stop == 0)
	  {
	    a = 1;
	  }
	RR_STOP_SIMPLE();
	na = 3490000000; //a;
	assert(a);
      }
      break;
    case 15:
      {
	na = 0;
	barrier_cross(d->barrier);
	volatile size_t a = 0;
	RR_START_SIMPLE();
	while(na++ <3490000000)
	  {
	    a++;
	  }
	RR_STOP_SIMPLE();
	/* na = 3490000000; */
      }
      break;
    case 1:
      barrier_cross(d->barrier);
  
      RR_START_SIMPLE();
      while(stop == 0)
	{
	  na++;
	  _mm_pause();
	}

      RR_STOP_SIMPLE();
      break;
    case 2:
      barrier_cross(d->barrier);
  
      RR_START_SIMPLE();
      while(stop == 0)
	{
	  na++;
	  _mm_mfence();
	}

      RR_STOP_SIMPLE();
      break;
    case 3:
      barrier_cross(d->barrier);
  
      RR_START_SIMPLE();
      while(stop == 0)
	{
	  na++;
	  _mm_pause();
	  _mm_pause();
	  _mm_pause();
	  _mm_pause();
	  _mm_pause();
	  _mm_pause();
	}

      RR_STOP_SIMPLE();
      break;
    case 4:
      barrier_cross(d->barrier);
  
      RR_START_SIMPLE();
      while(stop == 0)
	{
	  na++;
	  asm volatile ( "nop" );
	}

      RR_STOP_SIMPLE();
      break;
    case 5:
      barrier_cross(d->barrier);
  
      RR_START_SIMPLE();
      while(stop == 0)
	{
	  na++;
	  asm volatile ( "nop" );
	  asm volatile ( "nop" );
	  asm volatile ( "nop" );
	  asm volatile ( "nop" );
	  asm volatile ( "nop" );
	  asm volatile ( "nop" );
	}

      RR_STOP_SIMPLE();
      break;
    case 6:
      barrier_cross(d->barrier);
  
      RR_START_SIMPLE();
      while(stop == 0)
	{
	  na++;
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	}

      RR_STOP_SIMPLE();
      break;
    case 7:
      barrier_cross(d->barrier);
  
      RR_START_SIMPLE();
      while(stop == 0)
	{
	  na++;
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	}

      RR_STOP_SIMPLE();
      break;
    case 8:
      barrier_cross(d->barrier);
  
      RR_START_SIMPLE();
      while(stop == 0)
	{
	  na++;
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	}

      RR_STOP_SIMPLE();
      break;
    case 9:
      barrier_cross(d->barrier);
  
      RR_START_SIMPLE();
      while(stop == 0)
	{
	  na++;
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );	  asm volatile ( "nop" );
	}

      RR_STOP_SIMPLE();
      break;
    case 10:
      barrier_cross(d->barrier);
  
      RR_START_SIMPLE();
      while(stop == 0)
	{
	  na++;
	  _mm_lfence();
	}

      RR_STOP_SIMPLE();
      break;
    case 11:
      barrier_cross(d->barrier);
  
      RR_START_SIMPLE();
      while(stop == 0)
	{
	  na++;
	  _mm_lfence();
	}

      RR_STOP_SIMPLE();
      break;
    }

  RR_TERM();

  d->num_acquires = na;

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
      {"duration",                  required_argument, NULL, 'd'},
      {"num-threads",               required_argument, NULL, 'n'},
      {"power",                     required_argument, NULL, 'o'},
      {"scenario",                  required_argument, NULL, 's'},
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
      c = getopt_long(argc, argv, "hvl:d:n:w:a:p:c:o:s:", long_options, &i);

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
	case 's':
	  scenario = atoi(optarg);
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

if (verbose)
  {
    switch (scenario)
      {
      case 1:
	printf("#Scenario: spin with _mm_pause();\n");
	break;
      case 2:
	printf("#Scenario: spin with _mm_mfence();\n");
	break;
      case 3:
	printf("#Scenario: spin with 6 * _mm_pause();\n");
	break;
      case 4:
	printf("#Scenario: spin with nop();\n");
	break;
      case 5:
	printf("#Scenario: spin with 6 * nop();\n");
	break;
      case 6:
	printf("#Scenario: spin with 12 * nop();\n");
	break;
      case 7:
	printf("#Scenario: spin with 24 * nop();\n");
	break;
      case 8:
	printf("#Scenario: spin with 48 * nop();\n");
	break;
      case 9:
	printf("#Scenario: spin with 96 * nop();\n");
	break;
      case 10:
	printf("#Scenario: spin with _mm_lfence();\n");
	break;
      case 11:
	printf("#Scenario: spin with _mm_sfence();\n");
	break;
      case 12:
	printf("#Scenario: spin for a 3.5 billion iterations;\n");
	break;
      case 13:
	printf("#Scenario: spin with increasing a volatile;\n");
	break;
      case 14:
	printf("#Scenario: spin with storing to a volatile;\n");
	break;
      case 0:
      default:
	/* scenario = 0; */
	printf("#Scenario: just spin\n");
      }
  }

if ((data = (thread_data_t *)memalign(CACHE_LINE_SIZE, num_threads * sizeof(thread_data_t))) == NULL) 
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

  rapl_stats_t s;
  RR_STATS(&s);
duration = 1000 * s.duration[NUMBER_OF_SOCKETS];

  if (verbose)
    {
      printf("Duration      : %d (ms)\n", duration);
    }

  double thr = (double) (acquires * 1000.0 / duration);
  printf("#acquires     : %10lu ( %10.0f / s)\n", acquires, thr);


  double ppw0 = thr / s.power_package[NUMBER_OF_SOCKETS];
  double ppw1 = thr / s.power_pp0[NUMBER_OF_SOCKETS];
  double ppw2 = thr / s.power_rest[NUMBER_OF_SOCKETS];
  double eop0 = (1e9 * s.energy_package[NUMBER_OF_SOCKETS]) / acquires;
  double eop1 = (1e9 * s.energy_pp0[NUMBER_OF_SOCKETS]) / acquires;
  double eop2 = (1e9 * s.energy_rest[NUMBER_OF_SOCKETS]) / acquires;
  printf("#ppw (ops/W)  : %10.1f | %10.1f | %10.1f\n", ppw0, ppw1, ppw2);
  printf("#eop (nJ/op)  : %10f | %10f | %10f\n", eop0, eop1, eop2);

  /* Cleanup locks */
  free(threads);
  free(data);

  return 0;
}


    /* { */
      /* na += bytes[55]; */
      /* na += bytes[51]; */
      /* na += bytes[28]; */
      /* na += bytes[63]; */
      /* na += bytes[17]; */
      /* na += bytes[22]; */
      /* na += bytes[5]; */
      /* na += bytes[58]; */
      /* na += bytes[53]; */
      /* na += bytes[3]; */
      /* na += bytes[40]; */
      /* na += bytes[33]; */
      /* na += bytes[35]; */
      /* na += bytes[37]; */
      /* na += bytes[48]; */
      /* na += bytes[31]; */
      /* na += bytes[27]; */
      /* na += bytes[42]; */
      /* na += bytes[61]; */
      /* na += bytes[56]; */
      /* na += bytes[26]; */
      /* na += bytes[43]; */
      /* na += bytes[60]; */
      /* na += bytes[7]; */
      /* na += bytes[15]; */
      /* na += bytes[11]; */
      /* na += bytes[14]; */
      /* na += bytes[2]; */
      /* na += bytes[10]; */
      /* na += bytes[49]; */
      /* na += bytes[13]; */
      /* na += bytes[57]; */
      /* na += bytes[0]; */
      /* na += bytes[4]; */
      /* na += bytes[12]; */
      /* na += bytes[24]; */
      /* na += bytes[18]; */
      /* na += bytes[41]; */
      /* na += bytes[34]; */
      /* na += bytes[36]; */
      /* na += bytes[6]; */
      /* na += bytes[50]; */
      /* na += bytes[23]; */
      /* na += bytes[62]; */
      /* na += bytes[32]; */
      /* na += bytes[16]; */
      /* na += bytes[25]; */
      /* na += bytes[46]; */
      /* na += bytes[39]; */
      /* na += bytes[54]; */
      /* na += bytes[52]; */
      /* na += bytes[19]; */
      /* na += bytes[1]; */
      /* na += bytes[59]; */
      /* na += bytes[45]; */
      /* na += bytes[20]; */
      /* na += bytes[8]; */
      /* na += bytes[29]; */
      /* na += bytes[9]; */
      /* na += bytes[44]; */
      /* na += bytes[30]; */
      /* na += bytes[38]; */
      /* na += bytes[21]; */
      /* na += bytes[47]; */
    /* } */
