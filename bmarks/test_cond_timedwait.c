#include "lock_in.h"

static inline void 
__cdelay(size_t c)
{
  while (c--)
    {
      asm volatile ("");
    }
}

pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t co  = PTHREAD_COND_INITIALIZER;

int
main(int argc, char** argv)
{
  if (argc != 3)
    {
      printf("Usage: %s seconds nanoseconds\n", argv[0]);
      exit(-1);
    }
  uint sec = atoi(argv[1]);
  uint nsec = atoi(argv[2]);

  printf("timedwait for:\n");
  printf("seconds      : %d\n", sec);
  printf("nanoseconds  : %d\n", nsec);

  int i;
  for (i = 0; i < 10; i++)
    {
      pthread_mutex_lock(&mu);

      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_sec += sec;
      ts.tv_nsec += nsec;

      if(pthread_cond_timedwait(&co, &mu, &ts))
  	{
  	  printf("(%2d) ho ho :: time out\n", i);
  	}

      pthread_mutex_unlock(&mu);
    }

  return 1;
}
