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

#include "atomic_asm.h"

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

  /* Wait on barrier */

  switch (scenario)
    {
    case 0:
      if (!d->id) { printf("#Scenario: do {} while(1);\n"); }
      barrier_cross(d->barrier);
      do 
	{
	  ;
	} 
      while(1);
      break;
    case 13:
      if (!d->id) { printf("#Scenario: do {ra = 1} while(1);\n"); }
      barrier_cross(d->barrier);
      for (;;)
	{
	  asm volatile ("mov $1, %rax");
	}
      break;
    case 1:
      if (!d->id) { printf("#Scenario: do {nop} while(1);\n"); }
      barrier_cross(d->barrier);
      do 
	{
	  asm volatile ("nop");
	} 
      while(1);
      break;
    case 2:
      if (!d->id) { printf("#Scenario: for(;;) {}\n"); }
      barrier_cross(d->barrier);
      for (;;)
	{
	}
      break;
    case 3:
      if (!d->id) { printf("#Scenario: while(1)\n"); }
      barrier_cross(d->barrier);
      while (1)
	{
	}
      break;
    case 4:
      {
	asm volatile ("nop ; nop");
	if (!d->id) { printf("#Scenario: for() {ra = ma}\n"); }
	UNUSED register uint64_t ra asm("rax") = 0;
	volatile uint64_t ma = 1;
	barrier_cross(d->barrier);
	/* do  */
	for(;;)
	  {
	    ra = ma;
	  }      
      }
      break;
    case 5:
      {
	if (!d->id) { printf("#Scenario: ma = ra, rb = mb\n"); }
	UNUSED register uint64_t ra = 0, rb = 1;
	UNUSED volatile uint64_t ma = 1, mb = 1;
	barrier_cross(d->barrier);
	for (;;)
	  {
	    ma = ra;
	    rb = mb;
	  }

      }
      break;
    case 6:
      {
	if (!d->id) { printf("#Scenario: 2 loads, 2 stores\n"); }
	UNUSED register uint64_t ra = 0, rb = 1, rc = 2, rd = 3, re = 4;
	UNUSED	volatile uint64_t ma = 1, mb = 1, mc = 2, md = 3, me = 4;
	barrier_cross(d->barrier);

#define ACTION(a, b) b = a;
#define ACTION2(a, b) a = b;
	for (;;)
	  {
	    ACTION(ra,  ma);
	    ACTION(rb,  mb);
	    /* ACTION(rc,  mc); */
	    /* ACTION(rd,  md); */
	    /* ACTION2(ra,  ma); */
	    /* ACTION2(rb,  mb); */
	    ACTION2(rc,  mc);
	    ACTION2(rd,  md);

	  }	   

      }
      break;
    case 7:
      {
	if (!d->id) { printf("#Scenario: sleep(100)\n"); }
	barrier_cross(d->barrier);
	sleep(100);
      }
      break;
    case 8:
      {
	if (!d->id) { printf("#Scenario: CAS_U64\n"); }
	volatile ALIGNED(CACHE_LINE_SIZE) uint64_t ma[8] = {1};
	barrier_cross(d->barrier);

	for (;;)
	  {
	    cas_u64(ma, 0, 1);
	  }
      }
      break; 
    case 9:
      {
	if (!d->id) { printf("#Scenario: successfull CAS_U64\n"); }
	volatile ALIGNED(CACHE_LINE_SIZE) uint64_t ma[8] = {0};
	barrier_cross(d->barrier);

	for (;;)
	  {
	    cas_u64(ma, 0, 1);
	    cas_u64(ma, 1, 0);
	  }
      }
      break; 
    case 10:
      {
	if (!d->id) { printf("#Scenario: CAS_U8\n"); }
	volatile ALIGNED(CACHE_LINE_SIZE) uint8_t ma[64] = {1};
	barrier_cross(d->barrier);

	for (;;)
	  {
	    cas_u8(ma, 0, 1);
	  }
      }
      break; 
    case 11:
      {
	if (!d->id) { printf("#Scenario: FAI_U64\n"); }
	volatile ALIGNED(CACHE_LINE_SIZE) uint64_t ma[8] = {1};
	barrier_cross(d->barrier);

	for (;;)
	  {
	    FAI_U64(ma);
	  }
      }
      break; 
    case 12:
      {
	if (!d->id) { printf("#Scenario: SWAP_U64\n"); }
	volatile ALIGNED(CACHE_LINE_SIZE) uint64_t ma[8] = {1};
	barrier_cross(d->barrier);

	for (;;)
	  {
	    SWAP_U64(ma, 1);
	  }
      }
      break; 
    case 14:
      {
	if (!d->id) { printf("#Scenario: for() { ma = 1 };\n"); }
	UNUSED register uint64_t ra asm("rax") = 0;
	UNUSED volatile uint64_t ma = 1;
	barrier_cross(d->barrier);
	/* do  */
	for(;;)
	  {
	    ma = 1;
	  }      
	break;
      }
    case 15:
      {
	if (!d->id) { printf("#Scenario: for() { ra = 1; rb = 2 };\n"); }
	UNUSED volatile uint64_t ma = 1;
	barrier_cross(d->barrier);
	/* do  */
	for(;;)
	  {
	    asm volatile ("mov $1, %rax");
	    asm volatile ("mov $1, %rbx");
	  }   
	break;   
      }
    case 16:
      {
	if (!d->id) { printf("#Scenario: for() { rb, ra };\n"); }
	register uint64_t ra asm("rax") = 0;
	volatile uint64_t ma = 1, mb = 2;
	barrier_cross(d->barrier);
	/* do  */
	for(;;)
	  {
	    asm volatile ("mov $1, %rax");
	    ra = ma;
	  }   
	break;
      }
    }


  do 
    {
      asm volatile ("nop");
      /* m[0] = r[0]; */
      /* m[1] = r[1]; */
      /* m[2] = r[2]; */
      /* m[3] = r[3]; */
      /* r[4] = m[4];       */
      /* r[5] = m[5]; */
      /* r[6] = m[6]; */
      /* r[7] = m[7]; */

      /* mb = rb; */
      /* md = rd; */
      /* ra = ma; */
      /* rc = mc; */
      
      /* rc = mc; */
      /* rd = md; */
    } 
  while(1);

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
  int core = 9;
  set_cpu(the_cores[core]);
  RR_INIT(core);

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
#endif
  timeout.tv_sec = duration / 1000;
  timeout.tv_nsec = (duration % 1000) * 1000000;

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


  /* Start threads */
  barrier_cross(&barrier);

  RR_START_SIMPLE();
  gettimeofday(&start, NULL);
  if (duration > 0) 
    {
      nanosleep(&timeout, NULL);
    } 
  RR_STOP_SIMPLE();
  gettimeofday(&end, NULL);

  /* else { */
  /*         sigemptyset(&block_set); */
  /*         sigsuspend(&block_set); */
  /*     } */

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
  duration = 1000 * s.duration[0];

  if (verbose)
    {
      printf("Duration      : %d (ms)\n", duration);
    }

  double thr = (double) (acquires * 1000.0 / duration);
  printf("#acquires     : %10lu ( %10.0f / s)\n", acquires, thr);


  RR_TERM();
  RR_PRINT(power_print);


  /* double ppw0 = thr / s.power_package[0]; */
  /* double ppw1 = thr / s.power_pp0[0]; */
  /* double ppw2 = thr / s.power_rest[0]; */
  /* double eop0 = (1e9 * s.energy_package[0]) / acquires; */
  /* double eop1 = (1e9 * s.energy_pp0[0]) / acquires; */
  /* double eop2 = (1e9 * s.energy_rest[0]) / acquires; */
  /* printf("#ppw (ops/W)  : %10.1f | %10.1f | %10.1f\n", ppw0, ppw1, ppw2); */
  /* printf("#eop (nJ/op)  : %10f | %10f | %10f\n", eop0, eop1, eop2); */



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
