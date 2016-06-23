#!/bin/bash

res=$(sudo likwid-powermeter -c0 -M0 ./$@);

dur=$(echo "$res" | awk '/Runtime/ { print $2 }');
echo "Duration        (secs):" $dur;

thr=$(echo "$res" | awk '/#acquires/ { print $5 }');
echo "Throughput     (ops/s):" $thr;

pow=$(echo "$res" | awk '/Power/ { print $3 }');
echo "Power          (watts):" $pow;

eng=$(echo "$res" | awk '/Energy/ { print $3 }');
echo "Energy        (joules):" $eng;

ops=$(echo "$res" | awk '/#acquires/ { print $3 }');
jpo=$(echo "1000000 * $eng/$ops" | bc -l);
printf "Energy/Op  (mjoule/op): %.6f\n" $jpo;

ppw=$(echo "$thr/$pow" | bc -l);
printf "Perf/Power (thr/watts): %.2f\n" $ppw;

