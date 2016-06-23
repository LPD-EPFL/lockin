#include "utils.h"

const size_t n_ns_per_sec = 1e9;

int
main(int a, char** v)
{
  
  size_t repeat = 1024;
  struct timespec ns = { .tv_sec = 0, .tv_nsec = 1 };
  while (repeat--)
    {
      volatile ticks __s =  getticks();
      nanosleep(&ns, NULL);
      volatile ticks __e =  getticks();
      ticks __d = __e - __s;

      ns.tv_nsec += 100;
      if (ns.tv_nsec > n_ns_per_sec)
	{
	  ns.tv_sec++;
	  ns.tv_nsec /= n_ns_per_sec;
	}

      printf("nanosleep(%-10zu ns) lasts: %zu ticks = %f ns\n",
	     ns.tv_nsec, __d, __d / CORE_SPEED_GHZ);
    };

  return 0;
}
