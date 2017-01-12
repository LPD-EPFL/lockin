/*
 * File: glk_in.h
 * Authors: Jelena Antic <jelena.antic@epfl.ch>
 *          Georgios Chatzopoulos <georgios.chatzopoulos@epfl.ch>
 *          Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>
 *
 * Description: 
 *      An implementation of the Generic Lock (GLK), an adaptive lock that switches
 *      among the TICKET, MCS and MUTEX lock algorithms.
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

#ifndef _GLK_IN_H_
#define _GLK_IN_H_

#include "glk.h"

#define LOCK_IN_NAME "GLK"
#define REPLACE_MUTEX    1    /* ovewrite the pthread_[mutex|cond] functions */

#define GLK_INITIALIZER {				\
    .lock_type = GLK_INIT_LOCK_TYPE,			\
      .num_acquired = 0,				\
      .queue_total = 0,					\
      .ticket_lock = GLK_TICKET_LOCK_INITIALIZER,	\
      .mcs_lock = GLK_MCS_LOCK_INITIALIZER,		\
      .mutex_lock = GLK_MUTEX_INITIALIZER,	\
      }

#if REPLACE_MUTEX == 1
#  define pthread_mutex_init      glk_init
#  define pthread_mutex_destroy   glk_destroy
#  define pthread_mutex_lock      glk_lock
#  define pthread_mutex_timedlock glk_timedlock
#  define pthread_mutex_unlock    glk_unlock
#  define pthread_mutex_trylock   glk_trylock
#  define pthread_mutex_t         glk_t
#  undef  PTHREAD_MUTEX_INITIALIZER
#  define PTHREAD_MUTEX_INITIALIZER GLK_INITIALIZER

#  define pthread_cond_init       glk_cond_init
#  define pthread_cond_destroy    glk_cond_destroy
#  define pthread_cond_signal     glk_cond_signal
#  define pthread_cond_broadcast  glk_cond_broadcast
#  define pthread_cond_wait       glk_cond_wait
#  define pthread_cond_timedwait  glk_cond_timedwait
#  define pthread_cond_t          glk_cond_t
#  undef  PTHREAD_COND_INITIALIZER
#  define PTHREAD_COND_INITIALIZER GLK_COND_INITIALIZER
#endif


#endif
