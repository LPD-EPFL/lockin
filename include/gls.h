/*
 * File: gls.h
 * Authors: Jelena Antic <jelena.antic@epfl.ch>
 *          Georgios Chatzopoulos <georgios.chatzopoulos@epfl.ch>
 *          Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description: 
 *      An implementation of a Generic Locking Service that uses
 *	    cache-line hash table to store locked memory locations.
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

#ifndef _GLS_H_
#define _GLS_H_

#include <stdint.h>
#include <assert.h>
#include <stdarg.h>

/* #undef  PADDING */
/* #define PADDING 0 */

#include "atomic_ops.h"
#include "glk.h"
#include "mcs_glk_impl.h"
#include "ttas_glk_impl.h"
#include "tas_glk_impl.h"
#include "ticket_glk_impl.h"
#include "mutex_glk_impl.h"

#ifndef GLS_DEBUG_MODE
#  define GLS_DEBUG_MODE      GLS_DEBUG_NONE
#endif

#include "gls_debug.h"

#define DEFAULT_CLHT_SIZE   256
#define GLS_LOCK_CACHE      1

void gls_init(uint32_t num_locks);
void gls_free();
size_t gls_get_id();

void gls_lock_init(void* mem_addr);

void gls_lock(void* mem_addr);
void gls_lock_ttas(void* mem_addr);
void gls_lock_ticket(void* mem_addr);
void gls_lock_mcs(void* mem_addr);
void gls_lock_mutex(void* mem_addr);
void gls_lock_tas(void* mem_addr);
void gls_lock_tas_in(void* mem_addr);

int gls_trylock(void* mem_addr);
int gls_trylock_ttas(void *mem_addr);
int gls_trylock_ticket(void *mem_addr);
int gls_trylock_mcs(void *mem_addr);
int gls_trylock_mutex(void *mem_addr);
int gls_trylock_tas(void* mem_addr);
int gls_trylock_tas_in(void* mem_addr);

void gls_unlock(void* mem_addr);
void gls_unlock_ttas(void* mem_addr);
void gls_unlock_ticket(void* mem_addr);
void gls_unlock_mcs(void* mem_addr);
void gls_unlock_mutex(void* mem_addr);
void gls_unlock_tas(void* mem_addr);
void gls_unlock_tas_in(void* mem_addr);

#endif /* _GLS_H_ */
