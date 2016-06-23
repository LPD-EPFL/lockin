#!/bin/bash

. ./scripts/repeat_med.sh
. ./scripts/repeat_max.sh
. ./scripts/help

reps=11;
duration=5000;

cores=$(seq 3 1 40);

params_all="-l -p-1";		# no latency, no set_cpu
params=( "-s5" "-s6" "-s9 -i1" "-s9 -i10" "-s9 -i100" "-s9 -i1000" );


function do_run()
{
    r=$(./futex $@ -n$n $p -d$duration $params_all);
    pow=$(echo "$r" | awk '/Total power/ { print $6 }');
    thr=$(echo "$r" | awk '/#acquires/ { print $5 }');
    latw=0 #$(echo "$r" | awk '/ECDF-wake/ { print $5 }');
    lats=0 #$(echo "$r" | awk '/ECDF-sleep/ { print $5 }');    
    printf " %-6.2f %-10d %-10d %-10d\n" $pow $thr $latw $lats;
}


printf "%-5s %-39s %-39s %-39s %-39s %-39s %-39s\n" "#Cores" "${params[@]}";


for n in $cores;
do
    printf "%-5d " $n
    for ((__i = 0; __i < ${#params[@]}; __i++))
    do
	p=${params[$__i]};
	repeat_max $reps 2 do_run;
    done;
    echo "";
done;
