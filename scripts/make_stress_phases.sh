#!/bin/sh

. scripts/config;

VERBOSE=1;
LOCKS="TICKET MCS ADAPTIVE MUTEX MUTEXEE"

if [ $# -gt 0 ];
then
    LOCKS="$@";
fi;

all="stress_phase_in";

for lock in $LOCKS
do
    echo "Building: $lock";
    touch Makefile;
    if [ $VERBOSE -eq 1 ]; 
    then
	POWER=0 LOCK_IN=$lock $MAKE $all
    else
	POWER=0  LOCK_IN=$lock $MAKE $all  > /dev/null;
    fi

    for e in $all;
    do
	mv ${e} ${e}_${lock};
    done;
done;
