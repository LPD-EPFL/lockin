CFLAGS= -D_GNU_SOURCE -Wall -O3

ifeq ($(DEBUG),1)
  CFLAGS=-D_GNU_SOURCE -Wall -O0 -ggdb -DDEBUG -g -fno-inline
endif

ifneq ($(USE_HLE),)
  CFLAGS+=-DUSE_HLE=${USE_HLE}
endif

ifneq ($(LOCK_IN),)
  CFLAGS+=-DLOCK_IN=${LOCK_IN}
endif

ifneq ($(PAUSE_IN),)
  CFLAGS+=-DPAUSE_IN=${PAUSE_IN}
endif

ifeq ($(RTM),1)
  CFLAGS+=-DRTM -mrtm	
endif

ifeq ($(POWER),0)
  CFLAGS+=-DPOWER=0
else
  CFLAGS+=-DPOWER=1
endif

ifneq ($(TIMEOUT),)
  CFLAGS+=-DMUTEXEE_FTIMEOUT=${TIMEOUT}
endif

ifeq ($(PAD),1)
  CFLAGS+=-DPADDING=1
endif

UNAME:=$(shell uname -n)

ifeq ($(UNAME), lpdxeon2680)
  PLATFORM=-DXEON2
endif


ifndef PLATFORM
# PLATFORM=-DSPARC
# PLATFORM=-DTILERA
# PLATFORM=-DXEON
# PLATFORM=-DOPTERON
# PLATFORM=-DDEFAULT
PLATFORM=-DXEON2
endif

ifeq ($(UNAME), lpdpc4)
FREQ_GHZ:=3.49195
PLATFORM=-DDEFAULT
# FREQ_GHZ:=1.6
endif

ifeq ($(UNAME), lpdpc34)
FREQ_GHZ:=3.4
PLATFORM=-DDEFAULT
endif

ifeq ($(PLATFORM), -DDEFAULT)
CORE_NUM ?= $(shell nproc)
FREQ_GHZ ?= 3

CFLAGS+=-DFREQ_GHZ=${FREQ_GHZ}
ifneq ($(CORE_SPEED_KHz), )
CFLAGS+=-DCORE_NUM=${CORE_NUM} 
else
CFLAGS+=-DCORE_NUM=8
endif
$(info ********************************** Using as a default number of cores: $(CORE_NUM) on 1 socket)
$(info ********************************** Using as a default frequency      : $(FREQ_GHZ) GHz)
endif

RAPL=raplread
CFLAGS+=$(PLATFORM)

CC ?= gcc
LIBS:=-L. -lrt -lpthread -lnuma -l$(RAPL) -lm
LIBS_IN:=$(LIBS) -llockin -ldvfs_set


TOP:=$(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

SRC:=$(TOP)/src
INCLUDE:=$(TOP)/include

INCLUDES:=-I$(INCLUDE)

all: stress_one_in stress_test_in stress_latency_in stress_ldi_in\
	stress_queued_in stress_inc_cs stress_phase_in
	@echo "############### Used: " $(LOCK_IN) " on " $(PLATFORM) \
		" with " $(CFLAGS)

FORCE:

libraplread.a: FORCE
	./scripts/configure.sh

liblockin.a: mcs_in.o include/mcs_in.h clh_in.o include/clh_in.h
	ar -r liblockin.a mcs_in.o include/mcs_in.h clh_in.o include/clh_in.h

libmcs_in.a: mcs_in.o include/mcs_in.h
	ar -r libmcs_in.a mcs_in.o include/mcs_in.h

mcs_in.o: FORCE
	$(CC) $(CFLAGS) $(INCLUDES) -c src/mcs_in.c

libclh_in.a: clh_in.o include/clh_in.h
	ar -r libclh_in.a clh_in.o include/clh_in.h

clh_in.o: FORCE
	$(CC) $(CFLAGS) $(INCLUDES) -c src/clh_in.c

libdvfs_set.a: dvfs_set.o include/dvfs_set.h
	ar -r libdvfs_set.a dvfs_set.o include/dvfs_set.h

dvfs_set.o: FORCE
	$(CC) -c $(SRC)/dvfs_set.c $(CFLAGS) $(INCLUDES)	

libs: libraplread.a liblockin.a libdvfs_set.a


stress_correct_in: libs bmarks/stress_correct_in.c
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/stress_correct_in.c -o stress_correct_in $(LIBS_IN)

stress_rw_in: libs bmarks/stress_rw_in.c
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/stress_rw_in.c -o stress_rw_in $(LIBS_IN)

stress_rw_try_in: libs bmarks/stress_rw_try_in.c
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/stress_rw_try_in.c -o stress_rw_try_in $(LIBS_IN)

nanosleep: bmarks/nanosleep.c Makefile
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/nanosleep.c -o nanosleep $(LIBS)

test_cond_timedwait: bmarks/test_cond_timedwait.c 
	$(CC) -g $(CFLAGS) $(INCLUDES) bmarks/test_cond_timedwait.c -o test_cond_timedwait -pthread

stress_test_in: libs FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/stress_test_in.c -o stress_test_in $(LIBS_IN)

stress_inc_cs: libs FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/stress_inc_cs.c -o stress_inc_cs $(LIBS_IN)

stress_phase_in: libs FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/stress_phase_in.c -o stress_phase_in $(LIBS_IN)

stress_latency_in: libs FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/stress_latency_in.c -o stress_latency_in $(LIBS_IN)

cdf.o: $(SRC)/cdf.c $(INCLUDE)/cdf.h
	$(CC) -c $(SRC)/cdf.c $(CFLAGS) $(INCLUDES)	

stress_ldi_in: libs cdf.o FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/stress_ldi_in.c -o stress_ldi_in cdf.o $(LIBS_IN)

stress_mwait: libs FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/stress_mwait.c -o stress_mwait $(LIBS_IN)

stress_mwait_one: libs FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/stress_mwait_one.c -o stress_mwait_one $(LIBS_IN)

stress_mwait_queued: libs FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/stress_mwait_queued.c -o stress_mwait_queued $(LIBS_IN)


stress_one_in: libs libraplread.a FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/stress_one_in.c -o stress_one_in $(LIBS_IN)

stress_one_in_libc: libs FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/stress_one_in.c -o stress_one_in_libc -Wl,--rpath=/home/trigonak/code/libs/glibc-2.23/lib/ -Wl,--dynamic-linker=/home/trigonak/code/libs/glibc-2.23/lib/ld-linux-x86-64.so.2 -lpthread -lrt 


test_wakeup: libs FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/test_wakeup.c -o test_wakeup $(LIBS_IN)

test_wakeup_barrier: libs FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/test_wakeup_barrier.c -o test_wakeup_barrier $(LIBS_IN)

test_wakeup_unlock: libs FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/test_wakeup_unlock.c -o test_wakeup_unlock $(LIBS_IN)

test_spin_unlock: libs sbarrier.o FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/test_spin_unlock.c -o test_spin_unlock sbarrier.o $(LIBS_IN)

test_futex_unlock: libs sbarrier.o FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/test_futex_unlock.c -o test_futex_unlock sbarrier.o $(LIBS_IN)

test_tas_unlock: libs FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/test_tas_unlock.c -o test_tas_unlock $(LIBS_IN)

test_mutex_unlock: FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/test_mutex_unlock.c -o test_mutex_unlock sbarrier.o -L. -L/home/trigonak/code/glibc/install/lib -lpthread

test_mutex_lock: FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/test_mutex_lock.c -o test_mutex_lock sbarrier.o -L. -L/home/trigonak/code/glibc/install/lib -lpthread

test_mwait_unlock: FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/test_mwait_unlock.c -o test_mwait_unlock -L. -L/home/trigonak/code/glibc/install/lib -lpthread


test_wakeup_mutex: libs FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/test_wakeup_mutex.c -o test_wakeup_mutex $(LIBS_IN)

test_wakeup_mutex2: libs FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/test_wakeup_mutex2.c -o test_wakeup_mutex2 $(LIBS_IN)


test_queue_time: FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/test_queue_time.c -o stress_on_in_QUEUE $(LIBS_IN)

stress_queued_in: libs FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/stress_queued_in.c -o stress_queued_in $(LIBS_IN)

stress_context_switch: libs FORCE
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/stress_context_switch.c -o stress_context_switch $(LIBS_IN)



l1_spin: bmarks/l1_spin.c Makefile
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/l1_spin.c -o l1_spin $(LIBS)

l1_spin_cont: bmarks/l1_spin_cont.c Makefile include/atomic_asm.h
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/l1_spin_cont.c -o l1_spin_cont $(LIBS)

vstats: FORCE
	$(CC) $(CFLAGS) $(INCLUDES) src/vstats.c -o vstats $(LIBS)

sbarrier.o: $(SRC)/sbarrier.c $(INCLUDE)/sbarrier.h
	$(CC) -c $(SRC)/sbarrier.c $(CFLAGS) $(INCLUDES)	

cs: power/cs.c Makefile include/atomic_asm.h cdf.o 
	$(CC) $(CFLAGS) $(INCLUDES) power/cs.c -o context_switch cdf.o $(LIBS) $(PAPI)

dvfs_set: libs FORCE
	$(CC) $(CFLAGS) $(INCLUDES) power/dvfs_set.c -o dvfs_set cdf.o $(LIBS) $(PAPI) $(LIBS_IN)

futex: power/futex.c Makefile include/atomic_asm.h cdf.o sbarrier.o FORCE
	$(CC) $(CFLAGS) $(INCLUDES) power/futex.c -o futex cdf.o sbarrier.o $(LIBS) $(PAPI)

futex_pow_only: power/futex_pow_only.c Makefile include/atomic_asm.h cdf.o sbarrier.o FORCE
	$(CC) $(CFLAGS) $(INCLUDES) power/futex_pow_only.c -o futex_pow_only cdf.o sbarrier.o $(LIBS) $(PAPI)

mutex: power/mutex.c Makefile include/atomic_asm.h cdf.o sbarrier.o FORCE
	$(CC) $(CFLAGS) $(INCLUDES) power/mutex.c -o mutex cdf.o sbarrier.o $(LIBS) $(PAPI)

noise: power/noise.c Makefile include/atomic_asm.h cdf.o FORCE
	$(CC) $(CFLAGS) $(INCLUDES) power/noise.c -o noise $(LIBS)

placement_print: power/placement_print.c Makefile include/atomic_asm.h cdf.o FORCE
	$(CC) $(CFLAGS) $(INCLUDES) power/placement_print.c -o placement_print $(LIBS)

iddle_pwr: bmarks/iddle_pwr.c Makefile include/atomic_asm.h
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/iddle_pwr.c -o iddle_pwr $(LIBS)

full_cpu_pwr: bmarks/full_cpu_pwr.c Makefile include/atomic_asm.h
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/full_cpu_pwr.c -o full_cpu_pwr $(LIBS)


cas_spin: bmarks/cas_spin.c Makefile
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/cas_spin.c -o cas_spin $(LIBS)

sleep: bmarks/sleep.c Makefile
	$(CC) $(CFLAGS) $(INCLUDES) bmarks/sleep.c -o sleep $(LIBS)

energy: src/energy.c
	$(CC) -O3 -o energy src/energy.c -lm 

energy_sock: src/energy_sock.c
	$(CC) $(INCLUDES) -L. -O3 -o energy_sock src/energy_sock.c -lm -l$(RAPL) -lm


clean:
	rm -f *~ *.o stress_* lib* energy* nanosleep placement_print spin test* l1_*
