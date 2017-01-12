/*   
 * File: clht_lock_pointer.c
 * Authors: Jelena Antic <jelena.antic@epfl.ch>
 *          Georgios Chatzopoulos <georgios.chatzopoulos@epfl.ch>
 *          Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description:
 *     CLHT optimized for GLS - value type is a pointer to a lock, key type is a
 *     memory address.
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
 *
 */

#include <math.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <execinfo.h>

#include "clht_lock_pointer.h"

#ifdef DEBUG
__thread uint32_t put_num_restarts = 0;
__thread uint32_t put_num_failed_expand = 0;
__thread uint32_t put_num_failed_on_new = 0;
#endif

__thread size_t check_ht_status_steps = CLHT_STATUS_INVOK_IN;

#include "stdlib.h"
#include "assert.h"

static uint32_t clht_lp_put_seq(clht_lp_hashtable_t* hashtable,
				clht_lp_addr_t key,
				clht_lp_val_t val,
				uint64_t owner,
				uint64_t bin);
clht_lp_val_t clht_lp_put_impl(clht_lp_t* h, volatile bucket_t* bucket, clht_lp_addr_t key, uint put_type);

/* ******************************************************************************** */
/* help functions */
/* ******************************************************************************** */

const char*
clht_lp_type_desc()
{
  return "CLHT-LB-RESIZE";
}

inline int
is_power_of_two (unsigned int x) 
{
  return ((x != 0) && !(x & (x - 1)));
}

static inline
int is_odd (int x)
{
    return x & 1;
}

/** Jenkins' hash function for 64-bit integers. */
inline uint64_t
__ac_Jenkins_hash_64(uint64_t key)
{
    key += ~(key << 32);
    key ^= (key >> 22);
    key += ~(key << 13);
    key ^= (key >> 8);
    key += (key << 3);
    key ^= (key >> 15);
    key += ~(key << 27);
    key ^= (key >> 31);
    return key;
}

/* ******************************************************************************** */
/* deadlock and error detection */
/* ******************************************************************************** */

inline int
clht_lp_set_owner(clht_lp_hashtable_t* hashtable, clht_lp_addr_t key, size_t owner)
{
#if GLS_DEBUG_MODE == GLS_DEBUG_DEADLOCK
  size_t bin = clht_lp_hash(hashtable, key);
  volatile bucket_t* bucket = hashtable->table + bin;
  uint32_t j;

  do
    {
  for (j = 0; j < ENTRIES_PER_BUCKET; j++)
    {
  if (bucket->key[j] == key)
    {
  bucket->owner[j] = owner;
  return true;
}
}

  bucket = (bucket_t *) bucket->next;
}
  while (bucket != NULL);
#endif
  return false;
}


static inline void
clht_lp_ddd_waiting_set(clht_lp_t* h, clht_lp_addr_t addr)
{
  GLS_DDD(h->waiting[gls_get_id_arr()].addr = (void*) addr);
}

inline void
clht_lp_ddd_waiting_unset(clht_lp_t* h, void* addr)
{
  GLS_DDD
    (
     void* addr_waiting = h->waiting[gls_get_id_arr()].addr;
     if (likely(addr_waiting == addr))
       {
	 h->waiting[gls_get_id_arr()].addr = NULL;
       }
     else
       {
	 GLS_WARNING("wrong address, waiting on %p", "UNSET-WAIT", addr, addr_waiting);
       }
     );
}

static inline void*
clht_lp_ddd_waiting_get(clht_lp_t* h, const size_t tid)
{
  GLS_DDD(return h->waiting[tid - 1].addr);
  return NULL;
}

static UNUSED void*
clht_lp_ddd_get_owner(clht_lp_t* h, clht_lp_addr_t key, size_t* lock_owner)
{
#if GLS_DEBUG_MODE == GLS_DEBUG_DEADLOCK
     clht_lp_hashtable_t* hashtable = h->ht;
     size_t bin = clht_lp_hash(hashtable, key);
     volatile bucket_t* bucket = hashtable->table + bin;
     size_t owner = 0;
     do
       {
	 uint32_t j;
	 for (j = 0; j < ENTRIES_PER_BUCKET; j++)
	   {
	     if (bucket->key[j] == key)
	       {
		 owner = bucket->owner[j];
		 if (owner)
		   {
		     *lock_owner = owner;
		     break;
		   }
		 else
		   {
		     /* the owner field is not properly set yet */
		     return NULL;
		   }
	       }
	   }
	 bucket = (bucket_t *) bucket->next;
       }
     while (bucket != NULL);
     
     void* addr = NULL;
     if (owner > 0)
       {
	 addr = clht_lp_ddd_waiting_get(h, owner);
       }
     return addr;
#else
     return NULL;
#endif
}


void
clht_lp_ddd_print_backtrace()
{
  void* trace[16];
  char** messages = (char**) NULL;
  int i, trace_size = 0;

  trace_size = backtrace(trace, 16);
  messages = backtrace_symbols(trace, trace_size);
  printf("[BACKTRACE] Execution path:\n");
  for (i = 2; i < trace_size; i++)
    {
      printf("[BACKTRACE] #%d %s\n", i - 2, messages[i]);
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
}


void
clht_lp_ddd_check(clht_lp_t* h)
{
#if GLS_DEBUG_MODE == GLS_DEBUG_DEADLOCK
  if (TRYLOCK_ACQ(&h->dd_lock))
    {
      return;
    }

  /* GLS_DPRINT("(%zu) Will check for deadlock", gls_get_id());      */

  struct
  {
    size_t id;
    void* wait_on;
  } wait_cycle[GLS_MAX_NUM_THREDS];
  int ci = 0;
  const size_t myid = gls_get_id();

  void* wait_on = clht_lp_ddd_waiting_get(h, myid);
  wait_cycle[ci].id = myid;
  wait_cycle[ci++].wait_on = wait_on;

  size_t owner = 0;
  void* owner_wait_on;
  do
    {
      owner_wait_on = clht_lp_ddd_get_owner(h, (clht_lp_addr_t) wait_on, &owner);
      wait_cycle[ci].id = owner;
      wait_cycle[ci++].wait_on = owner_wait_on;
      if (owner == myid)
	{
	  GLS_WARNING("(%zu) cycle detected", "DEADLOCK", wait_cycle[0].wait_on, myid);
	  size_t id, i = 0, twice = 0;
	  do
	    {
	      id = wait_cycle[i].id;
	      printf("[%-3zu waits for %p] -> \n", id, wait_cycle[i].wait_on);
	      i++;
	      twice += (id == myid);
	    }
	  while (twice != 2);
	  printf(" ... \n");
	  clht_lp_ddd_print_backtrace();
	  exit (-2);
	}
      wait_on = owner_wait_on;
    }
  while (ci < GLS_MAX_NUM_THREDS && wait_on != NULL);

  TRYLOCK_RLS(h->dd_lock);
#endif	/* GLS_DEBUG_DEADLOCK */
}


/* ******************************************************************************** */
/* allocation functions */
/* ******************************************************************************** */

/* Create a new bucket. */
bucket_t*
clht_lp_bucket_create() 
{
  bucket_t* bucket = NULL;
  bucket = memalign(CACHE_LINE_SIZE, sizeof(bucket_t));
  /* bucket = malloc(sizeof(bucket_t)); */
  if (bucket == NULL)
    {
      return NULL;
    }

  bucket->lock = 0;

  uint32_t j;
  for (j = 0; j < ENTRIES_PER_BUCKET; j++)
    {
      bucket->key[j] = 0;
      GLS_DDD(bucket->owner[j] = 0;);
    }
  bucket->next = NULL;
    
  return bucket;
}

bucket_t*
clht_bucket_create_stats(clht_lp_hashtable_t* h, int* resize)
{
  bucket_t* b = clht_lp_bucket_create();
  if (IAF_U32(&h->num_expands) == h->num_expands_threshold)
    {
      /* printf("      -- hit threshold (%u ~ %u)\n", h->num_expands, h->num_expands_threshold); */
      *resize = 1;
    }
  return b;
}

clht_lp_hashtable_t* clht_lp_hashtable_create(uint64_t num_buckets);

clht_lp_t* 
clht_lp_create(uint64_t num_buckets)
{
  clht_lp_t* w = (clht_lp_t*) memalign(CACHE_LINE_SIZE, sizeof(clht_lp_t));
  if (w == NULL)
    {
      printf("** malloc @ hatshtalbe\n");
      return NULL;
    }

  w->ht = clht_lp_hashtable_create(num_buckets);
  if (w->ht == NULL)
    {
      free(w);
      return NULL;
    }
  w->resize_lock = LOCK_FREE;
  w->gc_lock = LOCK_FREE;
  w->status_lock = LOCK_FREE;
  w->version_list = NULL;
  w->version_min = 0;
  w->ht_oldest = w->ht;

  GLS_DDD
    (
     w->dd_lock = LOCK_FREE;
     gls_waiting_t* waiting = memalign(CACHE_LINE_SIZE, GLS_MAX_NUM_THREDS * sizeof(gls_waiting_t));
     if (waiting == NULL)
       {
	 GLS_WARNING("** failed\n", "MEMALIGN", NULL);
       }
     else
       {
	 int i;
	 for (i = 0; i < GLS_MAX_NUM_THREDS; i++)
	   {
	     waiting[i].addr = NULL;
	   }
	 w->waiting = waiting;
       }
     );
  return w;
}

clht_lp_hashtable_t* 
clht_lp_hashtable_create(uint64_t num_buckets) 
{
  clht_lp_hashtable_t* hashtable = NULL;
    
  if (num_buckets == 0)
    {
      return NULL;
    }
    
  /* Allocate the table itself. */
  hashtable = (clht_lp_hashtable_t*) memalign(CACHE_LINE_SIZE, sizeof(clht_lp_hashtable_t));
  if (hashtable == NULL) 
    {
      printf("** malloc @ hatshtalbe\n");
      return NULL;
    }
    
  /* hashtable->table = calloc(num_buckets, (sizeof(bucket_t))); */
  hashtable->table = (bucket_t*) memalign(CACHE_LINE_SIZE, num_buckets * (sizeof(bucket_t)));
  if (hashtable->table == NULL) 
    {
      printf("** alloc: hashtable->table\n"); fflush(stdout);
      free(hashtable);
      return NULL;
    }

  memset(hashtable->table, 0, num_buckets * (sizeof(bucket_t)));
    
  uint64_t i;
  for (i = 0; i < num_buckets; i++)
    {
      hashtable->table[i].lock = LOCK_FREE;
      uint32_t j;
      for (j = 0; j < ENTRIES_PER_BUCKET; j++)
	{
	  hashtable->table[i].key[j] = 0;
	  GLS_DDD(hashtable->table[i].owner[j] = 0;);
	}
    }

  hashtable->num_buckets = num_buckets;
  hashtable->hash = num_buckets - 1;
  hashtable->version = 0;
  hashtable->table_tmp = NULL;
  hashtable->table_new = NULL;
  hashtable->table_prev = NULL;
  hashtable->num_expands = 0;
  hashtable->num_expands_threshold = (CLHT_PERC_EXPANSIONS * num_buckets);
  if (hashtable->num_expands_threshold == 0)
    {
      hashtable->num_expands_threshold = 1;
    }
  hashtable->is_helper = 1;
  hashtable->helper_done = 0;
    
  return hashtable;
}

/* Hash a key for a particular hash table. */
inline uint64_t
clht_lp_hash(clht_lp_hashtable_t* hashtable, clht_lp_addr_t key) 
{
  return (key >> 2) & (hashtable->num_buckets - 1);
}

static inline clht_lp_val_t
bucket_exists(bucket_t* bucket, clht_lp_addr_t key)
{
  do 
    {
      uint32_t j;
      for (j = 0; j < ENTRIES_PER_BUCKET; j++) 
	{
	  if (bucket->key[j] == key) 
	    {
	      GLS_DDD
		(
		 if (unlikely(bucket->owner[j] == gls_get_id()))
		   {
		     GLS_WARNING("already owned by me (my id: %zu)",
				 "GET-LOCK", (void*) key, gls_get_id());
		   }
		 );      
	      return bucket->val[j];
	    }
	}

      bucket = (bucket_t *) bucket->next;
    } while (bucket != NULL);
  return 0;
}

static inline clht_lp_val_t*
bucket_exists_in(bucket_t* bucket, clht_lp_addr_t key)
{
  do 
    {
      uint32_t j;
      for (j = 0; j < ENTRIES_PER_BUCKET; j++) 
	{
	  if (bucket->key[j] == key) 
	    {
	      GLS_DDD
		(
		 if (unlikely(bucket->owner[j] == gls_get_id()))
		   {
		     GLS_WARNING("already owned by me (my id: %zu)",
				 "GET-LOCK", (void*) key, gls_get_id());
		   }
		 );      
	      return &bucket->val[j];
	    }
	}

      bucket = (bucket_t *) bucket->next;
    } while (bucket != NULL);
  return NULL;
}

  /* Retrieve a key-value entry from a hash table. */
inline clht_lp_val_t
clht_lp_get(clht_lp_hashtable_t* hashtable, clht_lp_addr_t key)
{
  size_t bin = clht_lp_hash(hashtable, key);
  bucket_t* bucket = hashtable->table + bin;
  uint32_t j;
  do 
    {
      for (j = 0; j < ENTRIES_PER_BUCKET; j++) 
	{
	  if (bucket->key[j] == key) 
	    {
	      GLS_DDD
		(
		 if (unlikely(bucket->owner[j] != gls_get_id()))
		   {
		     GLS_WARNING("wrong owner %zu (my id %zu)",
				 "GET-UNLOCK", (void*) key, bucket->owner[j], gls_get_id());
		   }
		 bucket->owner[j] = 0
		 );
	      return bucket->val[j];
	    }
	}

      bucket = (bucket_t *) bucket->next;
    } while (bucket != NULL);
  return 0;
}

  /* Retrieve a key-&value entry from a hash table. */
inline clht_lp_val_t*
clht_lp_get_in(clht_lp_hashtable_t* hashtable, clht_lp_addr_t key)
{
  size_t bin = clht_lp_hash(hashtable, key);
  bucket_t* bucket = hashtable->table + bin;
  do 
    {
      int j;
      for (j = 0; j < ENTRIES_PER_BUCKET; j++) 
	{
	  if (bucket->key[j] == key) 
	    {
	      GLS_DDD
		(
		 if (unlikely(bucket->owner[j] != gls_get_id()))
		   {
		     GLS_WARNING("wrong owner %zu (my id %zu)",
				 "GET-UNLOCK", (void*) key, bucket->owner[j], gls_get_id());
		   }
		 bucket->owner[j] = 0
		 );
	      return &bucket->val[j];
	    }
	}

      bucket = (bucket_t *) bucket->next;
    } while (bucket != NULL);
  return NULL;
}

inline clht_lp_val_t
clht_lp_put_type(clht_lp_t* h, clht_lp_addr_t key, const int type)
{
  clht_lp_ddd_waiting_set(h, key);
  clht_lp_hashtable_t* hashtable = h->ht;
  size_t bin = clht_lp_hash(hashtable, key);
  bucket_t* bucket = hashtable->table + bin;

  clht_lp_val_t curr_val = bucket_exists((bucket_t *) bucket, key);
  if (likely(curr_val != 0))
    {
      return curr_val;
    }

  return clht_lp_put_impl(h, bucket, key, type);
}


clht_lp_val_t
clht_lp_put_impl(clht_lp_t* h, volatile bucket_t* bucket, clht_lp_addr_t key, uint put_type)
{
  clht_lp_hashtable_t* hashtable = h->ht;
  clht_lp_lock_t* lock = (clht_lp_lock_t *) &bucket->lock;
  while (!LOCK_ACQ(lock, hashtable))
    {
      hashtable = h->ht;
      size_t bin = clht_lp_hash(hashtable, key);

      bucket = hashtable->table + bin;
      lock = (clht_lp_lock_t *) &bucket->lock;
    }

  CLHT_LP_GC_HT_VERSION_USED(hashtable);
  CLHT_CHECK_STATUS(h);
  clht_lp_addr_t* empty = NULL;
  clht_lp_val_t* empty_v = NULL;

  uint32_t j;

  do 
    {
      for (j = 0; j < ENTRIES_PER_BUCKET; j++) 
	{
	  if (bucket->key[j] == key) 
	    {
	      LOCK_RLS(lock);
	      return bucket->val[j];
	    }
	  else if (empty == NULL && bucket->key[j] == 0)
	    {
	      empty = (clht_lp_addr_t *) &bucket->key[j];
	      empty_v = &bucket->val[j];
	    }
	}
        
      int resize = 0;
      if (bucket->next == NULL)
	{
#if GLS_DEBUG_MODE >= GLS_DEBUG_NORMAL
	  int* keyv = (int*) key;
	  if (*keyv != GLS_DEBUG_LOCK_INIT)
	    {
	      GLS_WARNING("Uninitialized lock while locking", "LOCK", (void*) key);
	    }
#endif
    	  void* new_lock = NULL;
          // init and put a new lock
    	  switch (put_type) {
    	  case CLHT_PUT_ADAPTIVE:
	    new_lock = malloc(sizeof(glk_t));
	    assert(new_lock != NULL);
	    glk_init((glk_t *)new_lock, NULL);
	    break;
    	  case CLHT_PUT_TTAS:
	    new_lock = malloc(sizeof(ttas_lock_t));
	    assert(new_lock != NULL);
	    ttas_lock_init((ttas_lock_t *)new_lock, NULL);
	    break;
    	  case CLHT_PUT_TICKET:
	    new_lock = malloc(sizeof(ticket_lock_t));
	    assert(new_lock != NULL);
	    ticket_lock_init((ticket_lock_t *)new_lock, NULL);
	    break;
    	  case CLHT_PUT_MCS:
	    new_lock = malloc(sizeof(mcs_lock_t));
	    assert(new_lock != NULL);
	    mcs_lock_init((mcs_lock_t *)new_lock, NULL);
	    break;
    	  case CLHT_PUT_MUTEX:
	    new_lock = malloc(sizeof(mutex_lock_t));
	    assert(new_lock != NULL);
	    mutex_init((mutex_lock_t *)new_lock, NULL);
	    break;
    	  case CLHT_PUT_TAS:
	    new_lock = malloc(sizeof(tas_lock_t));
	    assert(new_lock != NULL);
	    tas_lock_init((tas_lock_t *)new_lock, NULL);
	    break;    	  
	  }

	  if (empty == NULL)
	    {
	      DPP(put_num_failed_expand);
	      bucket_t* b = clht_bucket_create_stats(hashtable, &resize);
	      b->val[0] = (clht_lp_val_t) new_lock;
#ifdef __tile__
	      /* keep the writes in order */
	      _mm_sfence();
#endif
	      b->key[0] = key;
#ifdef __tile__
	      /* make sure they are visible */
	      _mm_sfence();
#endif
	      bucket->next = b;
	    }
	  else 
	    {
	      *empty_v = (clht_lp_val_t) new_lock;
#ifdef __tile__
	      /* keep the writes in order */
	      _mm_sfence();
#endif
	      *empty = key;
	    }

	  LOCK_RLS(lock);
	  if (unlikely(resize))
	    {
	      /* ht_resize_pes(h, 1); */
	      ht_status(h, 1, 0);
	    }
	  return (clht_lp_val_t) new_lock;
	}
      bucket = bucket->next;
    }
  while (true);
}

int
clht_lp_put_init(clht_lp_t* h, clht_lp_addr_t key)
{
  clht_lp_hashtable_t* hashtable = h->ht;
  size_t bin = clht_lp_hash(hashtable, key);
  bucket_t* bucket = hashtable->table + bin;

  clht_lp_lock_t* lock = (clht_lp_lock_t *) &bucket->lock;
  while (!LOCK_ACQ(lock, hashtable))
    {
      hashtable = h->ht;
      size_t bin = clht_lp_hash(hashtable, key);

      bucket = hashtable->table + bin;
      lock = (clht_lp_lock_t *) &bucket->lock;
    }

  CLHT_LP_GC_HT_VERSION_USED(hashtable);
  CLHT_CHECK_STATUS(h);
  clht_lp_addr_t* empty = NULL;
  clht_lp_val_t* empty_v = NULL;

  uint32_t j;

  do 
    {
      for (j = 0; j < ENTRIES_PER_BUCKET; j++) 
	{
	  if (bucket->key[j] == key) 
	    {
	      LOCK_RLS(lock);
	      return 1;
	    }
	  else if (empty == NULL && bucket->key[j] == 0)
	    {
	      empty = (clht_lp_addr_t *) &bucket->key[j];
	      empty_v = &bucket->val[j];
	    }
	}
        
      int resize = 0;
      if (bucket->next == NULL)
	{
    	  void* new_lock = NULL;
	  new_lock = malloc(sizeof(glk_t));
	  assert(new_lock != NULL);
	  glk_init((glk_t *)new_lock, NULL);

	  if (empty == NULL)
	    {
	      DPP(put_num_failed_expand);
	      bucket_t* b = clht_bucket_create_stats(hashtable, &resize);
	      b->val[0] = (clht_lp_val_t) new_lock;
#ifdef __tile__
	      /* keep the writes in order */
	      _mm_sfence();
#endif
	      b->key[0] = key;
#ifdef __tile__
	      /* make sure they are visible */
	      _mm_sfence();
#endif
	      bucket->next = b;
	    }
	  else 
	    {
	      *empty_v = (clht_lp_val_t) new_lock;
#ifdef __tile__
	      /* keep the writes in order */
	      _mm_sfence();
#endif
	      *empty = key;
	    }

	  LOCK_RLS(lock);
	  if (unlikely(resize))
	    {
	      /* ht_resize_pes(h, 1); */
	      ht_status(h, 1, 0);
	    }
	  return 0;
	}
      bucket = (bucket_t*) bucket->next;
    }
  while (true);
}

inline clht_lp_val_t*
clht_lp_put_in(clht_lp_t* h, clht_lp_addr_t key) 
{
  clht_lp_ddd_waiting_set(h, key);
  clht_lp_hashtable_t* hashtable = h->ht;
  size_t bin = clht_lp_hash(hashtable, key);
  volatile bucket_t* bucket = hashtable->table + bin;

  clht_lp_val_t* curr_val = bucket_exists_in((bucket_t *) bucket, key);
  if (likely(curr_val != NULL))
    {
      return curr_val;
    }
  clht_lp_lock_t* lock = (clht_lp_lock_t *) &bucket->lock;
  while (!LOCK_ACQ(lock, hashtable))
    {
      hashtable = h->ht;
      size_t bin = clht_lp_hash(hashtable, key);

      bucket = hashtable->table + bin;
      lock = (clht_lp_lock_t *) &bucket->lock;
    }

  CLHT_LP_GC_HT_VERSION_USED(hashtable);
  CLHT_CHECK_STATUS(h);
  clht_lp_addr_t* empty = NULL;
  clht_lp_val_t* empty_v = NULL;

  do 
    {
      int j;
      for (j = 0; j < ENTRIES_PER_BUCKET; j++) 
	{
	  if (bucket->key[j] == key) 
	    {
	      LOCK_RLS(lock);
	      return &bucket->val[j];
	    }
	  else if (empty == NULL && bucket->key[j] == 0)
	    {
	      empty = (clht_lp_addr_t *) &bucket->key[j];
	      empty_v = &bucket->val[j];
	    }
	}
        
      int resize = 0;
      if (bucket->next == NULL)
	{
	  clht_lp_val_t* val = NULL;
	  if (empty == NULL)
	    {
	      DPP(put_num_failed_expand);
	      bucket_t* b = clht_bucket_create_stats(hashtable, &resize);
	      b->val[0] = 0;
	      val = &b->val[0];
#ifdef __tile__
	      /* keep the writes in order */
	      _mm_sfence();
#endif
	      b->key[0] = key;
#ifdef __tile__
	      /* make sure they are visible */
	      _mm_sfence();
#endif
	      bucket->next = b;
	    }
	  else 
	    {
	      *empty_v = 0;
	      val = empty_v;
#ifdef __tile__
	      /* keep the writes in order */
	      _mm_sfence();
#endif
	      *empty = key;
	    }

	  LOCK_RLS(lock);
	  if (unlikely(resize))
	    {
	      /* ht_resize_pes(h, 1); */
	      ht_status(h, 1, 0);
	    }
	  return val;
	}
      bucket = bucket->next;
    }
  while (true);
}

/* Remove a key-value entry from a hash table. */
clht_lp_val_t
clht_lp_remove(clht_lp_t* h, clht_lp_addr_t key)
{
  clht_lp_hashtable_t* hashtable = h->ht;
  size_t bin = clht_lp_hash(hashtable, key);
  bucket_t* bucket = hashtable->table + bin;

#if defined(READ_ONLY_FAIL)
  if (!bucket_exists(bucket, key))
    {
      return false;
    }
#endif  /* READ_ONLY_FAIL */

  clht_lp_lock_t* lock = &bucket->lock;
  while (!LOCK_ACQ(lock, hashtable))
    {
      hashtable = h->ht;
      size_t bin = clht_lp_hash(hashtable, key);

      bucket = hashtable->table + bin;
      lock = &bucket->lock;
    }

  CLHT_LP_GC_HT_VERSION_USED(hashtable);
  CLHT_CHECK_STATUS(h);
  uint32_t j;

  do 
    {
      for (j = 0; j < ENTRIES_PER_BUCKET; j++) 
	{
	  if (bucket->key[j] == key) 
	    {
	      clht_lp_val_t val = bucket->val[j];
	      bucket->key[j] = 0;
	      LOCK_RLS(lock);
	      return val;
	    }
	}
      bucket = (bucket_t *) bucket->next;
    } while (unlikely(bucket != NULL));
  LOCK_RLS(lock);
  return false;
}

void
clht_lp_destroy(clht_lp_hashtable_t* hashtable)
{
  uint32_t num_buckets = hashtable->num_buckets;
  bucket_t* bucket = NULL;

  uint32_t bin;
  for (bin = 0; bin < num_buckets; bin++)
    {
      bucket = hashtable->table + bin;
       
      uint32_t j; //, size = 0;
      do
	{
	  for (j = 0; j < ENTRIES_PER_BUCKET; j++)
	    {
	      if (bucket->key[j] > 0)
		{
                  // size++;
	    	 glk_destroy((glk_t *) bucket->val[j]);
                  free((glk_t *) bucket->val[j]);
		}
	    }

	  bucket = (bucket_t *) bucket->next;
	}
      while (bucket != NULL);
      // printf("size: %d\n", size);
    }

  free(hashtable->table);
  free(hashtable);
}

static int
bucket_cpy(volatile bucket_t* bucket, clht_lp_hashtable_t* ht_new)
{
  if (!LOCK_ACQ_RES((clht_lp_lock_t* )&bucket->lock))
    {
      return 0;
    }
  uint32_t j;
  do 
    {
      for (j = 0; j < ENTRIES_PER_BUCKET; j++) 
	{
	  clht_lp_addr_t key = bucket->key[j];
	  if (key != 0) 
	    {
	      uint64_t bin = clht_lp_hash(ht_new, key);
#if GLS_DEBUG_MODE == GLS_DEBUG_DEADLOCK
	      const size_t owner = bucket->owner[j];
#else
	      const size_t owner = 0;
#endif
	      clht_lp_put_seq(ht_new, key, bucket->val[j], owner, bin);
	    }
	}
      bucket = (bucket_t *) bucket->next;
    } 
  while (bucket != NULL);

  return 1;
}

static uint32_t
clht_lp_put_seq(clht_lp_hashtable_t* hashtable, clht_lp_addr_t key, clht_lp_val_t val, size_t owner, uint64_t bin)
{
  volatile bucket_t* bucket = hashtable->table + bin;
  uint32_t j;

  do
    {
      for (j = 0; j < ENTRIES_PER_BUCKET; j++)
	{
	  if (bucket->key[j] == 0)
	    {
	      bucket->val[j] = val;
	      bucket->key[j] = key;
	      GLS_DDD(bucket->owner[j] = owner;)
	      return true;
	    }
	}

      if (bucket->next == NULL)
	{
	  DPP(put_num_failed_expand);
	  int null;
	  bucket->next = clht_bucket_create_stats(hashtable, &null);
	  bucket->next->val[0] = val;
	  bucket->next->key[0] = key;
	  GLS_DDD(bucket->next->owner[0] = owner;)
	  return true;
	}

      bucket = (bucket_t *) bucket->next;
    }
  while (true);
}

void
ht_resize_help(clht_lp_hashtable_t* h)
{
  if ((int32_t) FAD_U32((volatile uint32_t*) &h->is_helper) <= 0)
    {
      return;
    }

  int32_t b;
  /* hash = num_buckets - 1 */
  for (b = h->hash; b >= 0; b--)
    {
      bucket_t* bu_cur = h->table + b;
      if (!bucket_cpy(bu_cur, h->table_tmp))
	{	    /* reached a point where the resizer is handling */
	  /* printf("[GC-%02d] helped  #buckets: %10zu = %5.1f%%\n",  */
	  /* 	 clht_lp_gc_get_id(), h->num_buckets - b, 100.0 * (h->num_buckets - b) / h->num_buckets); */
	  break;
	}
    }

  h->helper_done = 1;
}

int 
ht_resize_pes(clht_lp_t* h, int is_increase, int by)
{
  /* ticks s = getticks(); */

  check_ht_status_steps = CLHT_STATUS_INVOK;

  clht_lp_hashtable_t* ht_old = h->ht;

  if (TRYLOCK_ACQ(&h->resize_lock))
    {
      return 0;
    }

  size_t num_buckets_new;
  if (is_increase == true)
    {
      /* num_buckets_new = CLHT_RATIO_DOUBLE * ht_old->num_buckets; */
      num_buckets_new = by * ht_old->num_buckets;
    }
  else
    {
#if CLHT_HELP_RESIZE == 1
      ht_old->is_helper = 0;
#endif
      num_buckets_new = ht_old->num_buckets / CLHT_RATIO_HALVE;
    }

  printf("[CLHT] Resizing: from %8zu to %8zu buckets\n", ht_old->num_buckets, num_buckets_new);

  clht_lp_hashtable_t* ht_new = clht_lp_hashtable_create(num_buckets_new);
  ht_new->version = ht_old->version + 1;

#if CLHT_HELP_RESIZE == 1
  ht_old->table_tmp = ht_new; 

  int32_t b;
  for (b = 0; b < ht_old->num_buckets; b++)
    {
      bucket_t* bu_cur = ht_old->table + b;
      if (!bucket_cpy(bu_cur, ht_new)) /* reached a point where the helper is handling */
	{
	  break;
	}
    }

  if (is_increase && ht_old->is_helper != 1)	/* there exist a helper */
    {
      while (ht_old->helper_done != 1)
	{
	  _mm_pause();
	}
    }

#else

  int32_t b;
  for (b = 0; b < ht_old->num_buckets; b++)
    {
      bucket_t* bu_cur = ht_old->table + b;
      bucket_cpy(bu_cur, ht_new);
    }
#endif

#if defined(DEBUG)
  /* if (clht_size(ht_old) != clht_size(ht_new)) */
  /*   { */
  /*     printf("**clht_size(ht_old) = %zu != clht_size(ht_new) = %zu\n", clht_size(ht_old), clht_size(ht_new)); */
  /*   } */
#endif

  ht_new->table_prev = ht_old;

  int ht_resize_again = 0;
  if (ht_new->num_expands >= ht_new->num_expands_threshold)
    {
      /* printf("--problem: have already %u expands\n", ht_new->num_expands); */
      ht_resize_again = 1;
      /* ht_new->num_expands_threshold = ht_new->num_expands + 1; */
    }

  
  SWAP_U64((uint64_t*) h, (uint64_t) ht_new);
  ht_old->table_new = ht_new;
  TRYLOCK_RLS(h->resize_lock);

  //ticks e = getticks() - s;
  //double mba = (ht_new->num_buckets * 64) / (1024.0 * 1024);
  //printf("[RESIZE-%02d] to #bu %7zu = MB: %7.2f    | took: %13llu ti = %8.6f s\n",
	 //clht_lp_gc_get_id(), ht_new->num_buckets, mba, (unsigned long long) e, e / 2.1e9);


#if CLHT_DO_GC == 1
  //clht_lp_gc_collect(h);
#else
  //clht_lp_gc_release(ht_old);
#endif

  if (ht_resize_again)
    {
      ht_status(h, 1, 0);
    }

  return 1;
}

size_t
clht_lp_size(clht_lp_hashtable_t* hashtable)
{
  uint64_t num_buckets = hashtable->num_buckets;
  bucket_t* bucket = NULL;
  size_t size = 0;

  uint64_t bin;
  for (bin = 0; bin < num_buckets; bin++)
    {
      bucket = hashtable->table + bin;
       
      uint32_t j;
      do
	{
	  for (j = 0; j < ENTRIES_PER_BUCKET; j++)
	    {
	      if (bucket->key[j] > 0)
		{
		  size++;
		}
	    }

	  bucket = (bucket_t *) bucket->next;
	}
      while (bucket != NULL);
    }
  return size;
}

size_t
ht_status(clht_lp_t* h, int resize_increase, int just_print)
{
  if (TRYLOCK_ACQ(&h->status_lock) && !resize_increase)
    {
      return 0;
    }

  clht_lp_hashtable_t* hashtable = h->ht;
  uint64_t num_buckets = hashtable->num_buckets;
  volatile bucket_t* bucket = NULL;
  size_t size = 0;
  int expands = 0;
  int expands_max = 0;

  uint64_t bin;
  for (bin = 0; bin < num_buckets; bin++)
    {
      bucket = hashtable->table + bin;

      int expands_cont = -1;
      expands--;
      uint32_t j;
      do
	{
	  expands_cont++;
	  expands++;
	  for (j = 0; j < ENTRIES_PER_BUCKET; j++)
	    {
	      if (bucket->key[j] > 0)
		{
		  size++;
		}
	    }

	  bucket = (bucket_t *) bucket->next;
	}
      while (bucket != NULL);

      if (expands_cont > expands_max)
	{
	  expands_max = expands_cont;
	}
    }

  double full_ratio = 100.0 * size / ((hashtable->num_buckets) * ENTRIES_PER_BUCKET);

  if (just_print)
    {
      printf("[STATUS-%02d] #bu: %7zu / #elems: %7zu / full%%: %8.4f%% / expands: %4d / max expands: %2d\n",
	     99, hashtable->num_buckets, size, full_ratio, expands, expands_max);
    }
  else
    {
      if (full_ratio > 0 && full_ratio < CLHT_PERC_FULL_HALVE)
	{
	  //printf("[STATUS-%02d] #bu: %7zu / #elems: %7zu / full%%: %8.4f%% / expands: %4d / max expands: %2d\n",
		 //clht_lp_gc_get_id(), hashtable->num_buckets, size, full_ratio, expands, expands_max);
	  ht_resize_pes(h, 0, 33);
	}
      else if ((full_ratio > 0 && full_ratio > CLHT_PERC_FULL_DOUBLE) || expands_max > CLHT_MAX_EXPANSIONS ||
	       resize_increase)
	{
	  int inc_by = (full_ratio / CLHT_OCCUP_AFTER_RES);
	  int inc_by_pow2 = pow2roundup(inc_by);

	  //printf("[STATUS-%02d] #bu: %7zu / #elems: %7zu / full%%: %8.4f%% / expands: %4d / max expands: %2d\n",
		 //clht_lp_gc_get_id(), hashtable->num_buckets, size, full_ratio, expands, expands_max);
	  if (inc_by_pow2 == 1)
	    {
	      inc_by_pow2 = 2;
	    }
	  ht_resize_pes(h, 1, inc_by_pow2);
	}
    }

  if (!just_print)
    {
      //clht_lp_gc_collect(h);
    }

  TRYLOCK_RLS(h->status_lock);
  return size;
}


size_t
clht_size_mem(clht_lp_hashtable_t* h) /* in bytes */
{
  if (h == NULL)
    {
      return 0;
    }

  size_t size_tot = sizeof(clht_lp_hashtable_t**);
  size_tot += (h->num_buckets + h->num_expands) * sizeof(bucket_t);
  return size_tot;
}

size_t
clht_size_mem_garbage(clht_lp_hashtable_t* h) /* in bytes */
{
  if (h == NULL)
    {
      return 0;
    }

  size_t size_tot = 0;
  clht_lp_hashtable_t* cur = h->table_prev;
  while (cur != NULL)
    {
      size_tot += clht_size_mem(cur);
      cur = cur->table_prev;
    }

  return size_tot;
}

void
clht_lp_print(clht_lp_hashtable_t* hashtable)
{
  uint64_t num_buckets = hashtable->num_buckets;
  bucket_t* bucket;

  printf("Number of buckets: %" PRIu64 "\n", num_buckets);

  uint64_t bin;
  for (bin = 0; bin < num_buckets; bin++)
    {
      bucket = hashtable->table + bin;
      
      printf("[[%05zu]] ", bin);

      uint32_t j;
      do
	{
	  for (j = 0; j < ENTRIES_PER_BUCKET; j++)
	    {
	      if (bucket->key[j])
	      	{
		  printf("(%p)-> ", (void *) bucket->key[j]);
		}
	    }

	  bucket = (bucket_t *) bucket->next;
	  printf(" ** -> ");
	}
      while (bucket != NULL);
      printf("\n");
    }
  fflush(stdout);
}
