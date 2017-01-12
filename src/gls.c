/*
 * File: gls.c
 * Authors: Jelena Antic <jelena.antic@epfl.ch>
 *          Georgios Chatzopoulos <georgios.chatzopoulos@epfl.ch>
 *          Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description:
 *     An implementation of a Generic Locking Service (GLS) that uses
 *     cache-line hash table to store locked memory locations.
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Jelena Antic, Georgios Chatzopoulos, Vasileios Trigonakis
 *               Distributed Programming Lab (LPD), EPFL
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "gls.h"
#include "clht_lock_pointer.h"


clht_lp_t* gls_hashtable;

#if GLS_DEBUG_MODE == GLS_DEBUG_DEADLOCK
static size_t __gls_ids = 0;
static __thread size_t __gls_id = 0;

inline size_t
gls_get_id()
{
  if (unlikely(__gls_id == 0))
    {
      __gls_id = __sync_add_and_fetch(&__gls_ids, 1);
    }
  return __gls_id;
}

#  define GLS_DD_SET_OWNER(mem_addr)					\
  clht_lp_ddd_waiting_unset(gls_hashtable, mem_addr);			\
  if (unlikely(!clht_lp_set_owner(gls_hashtable->ht, (clht_lp_addr_t) mem_addr, gls_get_id())))	\
    {									\
      GLS_WARNING("could not set", "SET-OWNER", mem_addr);		\
    }

#  define GLS_DD_SET_OWNER_IF(ret, mem_addr)				\
  clht_lp_ddd_waiting_unset(gls_hashtable, mem_addr);			\
  if ((ret == 0) &&							\
      unlikely(!clht_lp_set_owner(gls_hashtable->ht, (clht_lp_addr_t) mem_addr, gls_get_id()))) \
    {									\
      GLS_WARNING("could not set", "SET-OWNER", mem_addr);		\
    }

#  undef GLS_LOCK_CACHE
#  define GLS_LOCK_CACHE 0

#else /* ! GLS_DEBUG_DEADLOCK*/
#  define GLS_DD_SET_OWNER(mem_addr)
#  define GLS_DD_SET_OWNER_IF(res, mem_addr)
#endif	/* GLS_DEBUG_DEADLOCK */


#if GLS_LOCK_CACHE == 1
struct gls_lock_cache
{
  void* lock;
  void* addr;
};
static __thread struct gls_lock_cache __thread_lock_cache = { .lock = NULL, .addr = NULL };
#endif

static inline void
gls_lock_cache_set(void* lock, void* address)
{
#if GLS_LOCK_CACHE == 1
  __thread_lock_cache.lock = lock;
  __thread_lock_cache.addr = address;
#endif
}

#if GLS_LOCK_CACHE == 1
#  define GLS_LOCK_CACHE_GET(mem_addr)			\
  if (likely(__thread_lock_cache.lock == mem_addr))	\
    {							\
      return __thread_lock_cache.addr;			\
    }
#else
#  define GLS_LOCK_CACHE_GET(mem_addr)		       
#endif

/* *********************************************************************************************** */
/* help functions */
/* *********************************************************************************************** */

static inline void*
gls_lock_get_put(clht_lp_t* gls_hashtable, void* mem_addr, const int lock_type)
{
  GLS_LOCK_CACHE_GET(mem_addr);

  void* lock_addr = (void*) clht_lp_put_type(gls_hashtable, (clht_lp_addr_t) mem_addr, lock_type);
#if GLS_LOCK_CACHE == 1
  gls_lock_cache_set(mem_addr, lock_addr);
#endif
  return lock_addr;
}

static inline void*
gls_lock_get_put_in(clht_lp_t* gls_hashtable, void* mem_addr)
{
  void* lock_addr = (void*) clht_lp_put_in(gls_hashtable, (clht_lp_addr_t) mem_addr);
#if GLS_LOCK_CACHE == 1
  gls_lock_cache_set(mem_addr, lock_addr);
#endif
  return lock_addr;
}

static inline void*
gls_lock_get_get(clht_lp_t* gls_hashtable, void* mem_addr, int lock_type, const char* from)
{
  GLS_LOCK_CACHE_GET(mem_addr);

  void* lock_addr = (void*) clht_lp_get(gls_hashtable->ht, (clht_lp_addr_t) mem_addr);
  GLS_DEBUG
    (
     if (unlikely(lock_addr == NULL))
       {
	 GLS_WARNING("not initialized in %s", "UNLOCK", mem_addr, from);
	 lock_addr = (void*) gls_lock_get_put(gls_hashtable, mem_addr, lock_type);
       }
     );

  return lock_addr;
}

static inline void*
gls_lock_get_get_in(clht_lp_t* gls_hashtable, void* mem_addr, const char* from)
{
  void* lock_addr = (void*) clht_lp_get_in(gls_hashtable->ht, (clht_lp_addr_t) mem_addr);
  GLS_DEBUG
    (
     if (unlikely(lock_addr == NULL))
       {
	 GLS_WARNING("not initialized in %s", "UNLOCK", mem_addr, from);
	 lock_addr = (void*) gls_lock_get_put_in(gls_hashtable, mem_addr);
       }
     );

  return lock_addr;
}



/* *********************************************************************************************** */
/* init functions */
/* *********************************************************************************************** */

static volatile int gls_initialized = 0;
static volatile int gls_initializing = 0;

/* unlikely was hurting performance at some test :-( */
#define GLS_INIT_ONCE()				\
  if (likely(gls_initialized == 0))		\
    {						\
      gls_init(DEFAULT_CLHT_SIZE);		\
    }

void gls_init(uint32_t num_locks) {
  if (__sync_val_compare_and_swap(&gls_initializing, 0, 1) == 1) {
    while (gls_initialized == 0)
      {
	PAUSE_IN();
      }
    return;
  }

  assert(gls_initialized == 0);

  uint32_t num_buckets = num_locks == 0
    ? DEFAULT_CLHT_SIZE
    : num_locks; // (num_locks + ENTRIES_PER_BUCKET - 1) /  ENTRIES_PER_BUCKET;
  gls_hashtable = clht_lp_create(num_buckets);
  assert(gls_hashtable != NULL);
  gls_initialized = 1;
  GLS_DPRINT("Initialized");
}

void gls_free() 
{
  if (gls_initialized)
    {
      clht_lp_destroy(gls_hashtable->ht);
    }
}


void gls_lock_init(void* mem_addr) 
{
  GLS_INIT_ONCE();
  if (clht_lp_put_init(gls_hashtable, (clht_lp_addr_t) mem_addr))
    {
      GLS_WARNING("Double initialization", "LOCK-INIT", mem_addr);
    }
}


/* *********************************************************************************************** */
/* lock functions */
/* *********************************************************************************************** */

inline void gls_lock(void* mem_addr) 
{
  GLS_INIT_ONCE();

  glk_t *lock =
    (glk_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_ADAPTIVE);
  glk_lock(lock);
  GLS_DD_SET_OWNER(mem_addr);
}

inline void gls_lock_ttas(void* mem_addr) 
{
  GLS_INIT_ONCE();

  ttas_lock_t *lock = (ttas_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_TTAS);
  ttas_lock_lock(lock);
  GLS_DD_SET_OWNER(mem_addr);
}

inline void gls_lock_tas(void* mem_addr) 
{
  GLS_INIT_ONCE();

  tas_lock_t *lock = (tas_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_TAS);
  tas_lock_lock(lock);
  GLS_DD_SET_OWNER(mem_addr);
}

inline void gls_lock_ticket(void* mem_addr) 
{
  GLS_INIT_ONCE();

  ticket_lock_t *lock = (ticket_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_TICKET);
  ticket_lock_lock(lock);
  GLS_DD_SET_OWNER(mem_addr);
}

inline void gls_lock_mcs(void* mem_addr) 
{
  GLS_INIT_ONCE();

  mcs_lock_t *lock = (mcs_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_MCS);
  mcs_lock_lock(lock);
  GLS_DD_SET_OWNER(mem_addr);
}

inline void gls_lock_mutex(void* mem_addr) 
{
  GLS_INIT_ONCE();

  mutex_lock_t *lock = (mutex_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_MUTEX);
  mutex_lock(lock);
  GLS_DD_SET_OWNER(mem_addr);
}



/* *********************************************************************************************** */
/* trylock functions */
/* *********************************************************************************************** */

inline int gls_trylock(void *mem_addr) 
{
  GLS_INIT_ONCE();

  glk_t *lock = 
    (glk_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_ADAPTIVE);
  const int res = glk_trylock(lock);
  GLS_DD_SET_OWNER_IF(res, mem_addr);
  return res;
}

inline int gls_trylock_ttas(void *mem_addr) 
{
  GLS_INIT_ONCE();

  ttas_lock_t *lock = (ttas_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_TTAS);
  const int res = ttas_lock_trylock(lock);
  GLS_DD_SET_OWNER_IF(res, mem_addr);
  return res;
}

inline int gls_trylock_tas(void *mem_addr) 
{
  GLS_INIT_ONCE();

  tas_lock_t *lock = (tas_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_TAS);
  const int res = tas_lock_trylock(lock);
  GLS_DD_SET_OWNER_IF(res, mem_addr);
  return res;
}

inline int gls_trylock_ticket(void *mem_addr) 
{
  GLS_INIT_ONCE();

  ticket_lock_t *lock = (ticket_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_TICKET);
  const int res = ticket_lock_trylock(lock);
  GLS_DD_SET_OWNER_IF(res, mem_addr);
  return res;
}

inline int gls_trylock_mcs(void *mem_addr) 
{
  GLS_INIT_ONCE();

  mcs_lock_t *lock = (mcs_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_MCS);
  const int res = mcs_lock_trylock(lock);
  GLS_DD_SET_OWNER_IF(res, mem_addr);
  return res;
}

inline int gls_trylock_mutex(void *mem_addr) 
{
  GLS_INIT_ONCE();

  mutex_lock_t *lock = (mutex_lock_t *) gls_lock_get_put(gls_hashtable, mem_addr, CLHT_PUT_MUTEX);
  const int res = mutex_lock_trylock(lock);
  GLS_DD_SET_OWNER_IF(res, mem_addr);
  return res;
}




/* *********************************************************************************************** */
/* unlock functions */
/* *********************************************************************************************** */

inline void gls_unlock(void* mem_addr) 
{
  GLS_INIT_ONCE();

  glk_t *lock = 
    (glk_t *) gls_lock_get_get(gls_hashtable, mem_addr, CLHT_PUT_ADAPTIVE, __FUNCTION__);
  GLS_DEBUG
    (
     if (unlikely(glk_is_free(lock)))
       {
	 GLS_WARNING("already free", "UNLOCK", mem_addr);
	 GLS_WARNING("skip cause LOAD lock could break", "UNLOCK", mem_addr);
	 return;
       }
     );
  glk_unlock(lock);
}

inline void gls_unlock_ttas(void* mem_addr) 
{
  GLS_INIT_ONCE();

  ttas_lock_t *lock = (ttas_lock_t *) gls_lock_get_get(gls_hashtable, mem_addr, CLHT_PUT_TTAS, __FUNCTION__);
  GLS_DEBUG
    (
     if (unlikely(ttas_lock_is_free(lock)))
       {
	 GLS_WARNING("already free", "UNLOCK", mem_addr);
       }
     );  
  ttas_lock_unlock(lock);
}

inline void gls_unlock_tas(void* mem_addr) 
{
  GLS_INIT_ONCE();

  tas_lock_t *lock = (tas_lock_t *) gls_lock_get_get(gls_hashtable, mem_addr, CLHT_PUT_TAS, __FUNCTION__);
  GLS_DEBUG
    (
     if (unlikely(tas_lock_is_free(lock)))
       {
	 GLS_WARNING("already free", "UNLOCK", mem_addr);
       }
     );  
  tas_lock_unlock(lock);
}

inline void gls_unlock_ticket(void* mem_addr) 
{
  GLS_INIT_ONCE();

  ticket_lock_t *lock =
    (ticket_lock_t *) gls_lock_get_get(gls_hashtable, mem_addr, CLHT_PUT_TICKET, __FUNCTION__);
  GLS_DEBUG
    (
     if (unlikely(ticket_lock_is_free(lock)))
       {
	 GLS_WARNING("already free", "UNLOCK", mem_addr);
       }
     );  
  ticket_lock_unlock(lock);
}

inline void gls_unlock_mcs(void* mem_addr) 
{
  GLS_INIT_ONCE();

  mcs_lock_t *lock = (mcs_lock_t *) gls_lock_get_get(gls_hashtable, mem_addr, CLHT_PUT_MCS, __FUNCTION__);
  GLS_DEBUG
    (
     if (unlikely(mcs_lock_is_free(lock)))
       {
	 GLS_WARNING("already free", "UNLOCK", mem_addr);
	 GLS_WARNING("skip cause MCS lock could break", "UNLOCK", mem_addr);
	 return;
       }
     );  
  mcs_lock_unlock(lock);
}

inline void gls_unlock_mutex(void* mem_addr) 
{
  GLS_INIT_ONCE();

  mutex_lock_t *lock =
    (mutex_lock_t *) gls_lock_get_get(gls_hashtable, mem_addr, CLHT_PUT_MUTEX, __FUNCTION__);
  GLS_DEBUG
    (
     if (unlikely(mutex_lock_is_free(lock)))
       {
	 GLS_WARNING("already free", "UNLOCK", mem_addr);
       }
     );  
  mutex_unlock(lock);
}

/* ******************************************************************************** */
/* lock inlined fucntions */
/* ******************************************************************************** */

typedef  volatile clht_lp_val_t gls_tas_t;

#define GLS_TAS_FREE   0
#define GLS_TAS_LOCKED 1
#define GLS_CAS(a, b, c) __sync_val_compare_and_swap(a, b, c)

inline void gls_lock_tas_in(void* mem_addr)
{
  GLS_INIT_ONCE();

  gls_tas_t* lock = (gls_tas_t*) gls_lock_get_put_in(gls_hashtable, mem_addr);
  if (likely(GLS_CAS(lock, GLS_TAS_FREE, GLS_TAS_LOCKED) == GLS_TAS_FREE))
    {
      GLS_DD_SET_OWNER(mem_addr);
      return;
    }

  do
    {
      asm volatile ("pause");
    }
  while (GLS_CAS(lock, GLS_TAS_FREE, GLS_TAS_LOCKED) != GLS_TAS_FREE);
  GLS_DD_SET_OWNER(mem_addr);
}

inline int gls_trylock_tas_in(void* mem_addr)
{
  GLS_INIT_ONCE();

  gls_tas_t* lock = (gls_tas_t*) gls_lock_get_put_in(gls_hashtable, mem_addr);
  int res = (GLS_CAS(lock, GLS_TAS_FREE, GLS_TAS_LOCKED) != GLS_TAS_FREE);
  GLS_DD_SET_OWNER_IF(res, mem_addr);
  return res;
}

inline void gls_unlock_tas_in(void* mem_addr)
{
  GLS_INIT_ONCE();
  gls_tas_t* lock = (gls_tas_t*) gls_lock_get_get_in(gls_hashtable, mem_addr, __FUNCTION__);
  GLS_DEBUG
    (
     if (unlikely(*lock == GLS_TAS_FREE))
       {
     	 GLS_WARNING("already free", "UNLOCK", mem_addr);
       }
     );

  asm volatile ("" ::: "memory");
  *lock = GLS_TAS_FREE;
  asm volatile ("" ::: "memory");
}


/* help */

#include <execinfo.h>

void
gls_print_backtrace()
{
#if GLS_DEBUG_BACKTRACE == 1
  void* trace[16];
  char** messages = (char**) NULL;
  int i, trace_size = 0;

  trace_size = backtrace(trace, 16);
  messages = backtrace_symbols(trace, trace_size);
  printf("   [BACKTRACE] Execution path:\n");
  for (i = 0; i < trace_size; i++)
    {
      printf("   [BACKTRACE] #%d %s\n", i - 2, messages[i]);
      /* find first occurence of '(' or ' ' in message[i] and assume
       * everything before that is the file name. (Don't go beyond 0 though
       * (string terminator)*/
      int p = 0;
      while(messages[i][p] != '(' && messages[i][p] != ' '
	    && messages[i][p] != 0)
	++p;

      char syscom[256];
      sprintf(syscom,"addr2line %p -e %.*s", trace[i], p, messages[i]);
      //last parameter is the file name of the symbol
      UNUSED int r = system(syscom);
    }
#endif
}
