#!/bin/bash

source scripts/config;

reps=$1;
shift;
prog="$1";
shift;
params="$@";

un=$(uname -n);
tmp=/tmp/run_rep_pow_max.${un}.tmp
printf '' > $tmp;

for r in $(seq 1 1 $reps);
do
    out=$($run_script ./$prog $params);
    thr=$(echo "$out" | awk '/#acquires/ { print $5 }');
    pow=$(echo "$out" | awk '/Total power/ { print $6 }');
    ppw=$(echo "$thr/$pow" | bc -l);
    eop=$(echo "$out" | awk '/#eop/ { print $4 }');
    llo=$(echo "$out" | awk '/#lock ticks/ { print $4 }');
    lun=$(echo "$out" | awk '/#unlock ticks/ { print $4 }');

    printf "%d %.2f %f %f %d %d \n" $thr $ppw $pow $eop $llo $lun >> $tmp;
done;

sort -n $tmp | grep -v "-" | head -n1

