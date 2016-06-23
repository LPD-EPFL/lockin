//test-and-test-and-set lock with backoff

#ifndef _MUTEX_H_
#define _MUTEX_H_

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#ifndef __sparc__
#include <numa.h>
#endif
#include <pthread.h>
#include "atomic_ops.h"
#include "utils.h"
#include <malloc.h>

typedef struct mutex_lock_t
{
  union 
  {
    pthread_mutex_t lock;
#ifdef ADD_PADDING
    uint8_t padding[CACHE_LINE_SIZE];
#endif
  };
} mutex_lock_t;

#endif


