#!/bin/bash

res=$(sudo likwid-powermeter -c0 -M0 ./$@);
dur=$(echo "$res" | awk '/Runtime/ { print $2 }');
thr=$(echo "$res" | awk '/#acquires/ { print $5 }');
pow=$(echo "$res" | awk '/Power/ { print $3 }');
eng=$(echo "$res" | awk '/Energy/ { print $3 }');
ops=$(echo "$res" | awk '/#acquires/ { print $3 }');
jpo=$(echo "1000000 * $eng/$ops" | bc -l);
ppw=$(echo "$thr/$pow" | bc -l);

printf "%-4.4f %-14d %-8.4f %-9.4f %-13.6f %-14.2f\n" $dur $thr $pow $eng $jpo $ppw;
