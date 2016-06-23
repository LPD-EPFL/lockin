#!/bin/bash

num_locks="1 8 64 512 4096 16384 131072 1048576";
num_cl="0 1 2 4";
read_write="0 1";
duration=3000;

num_locks="1 8 64 512 4096 16384 131072 1048576";
num_cl="2 4";
read_write="0 1";
duration=1000;


for cl in $num_cl
do
    echo "Accessing $cl cache lines";
    for rw in $read_write
    do
	echo "  Writing? $rw"
	for nl in $num_locks 
	do
	    kb=$(echo "($nl * 64) / 1024" | bc -l);
	    printf "    Sizeof locks in kb: %.3f\n" $kb;
	    out="data/energy_all_locks.c$cl.w$rw.l$nl.dat"
	    echo "      $out";
	    ./scripts/run_energy_all_locks.sh -d$duration -c$cl -w$rw -l$nl | tee $out;
	done;
    done;
done;
