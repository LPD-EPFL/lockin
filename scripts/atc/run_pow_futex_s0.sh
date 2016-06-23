#!/bin/bash

. ./scripts/repeat_med.sh

reps=1;
duration=1000;
scenario=0;

points="0";
max=$(echo "2^25" | bc)
s=1;
while [ $s -le $max ];
do
    points="$points $s";
    s=$((s*2));
done;
cores="10"

printf "%-11s %-32d \n" "#delay" $cores;

function do_run()
{
    r=$(./futex $@ -c$p -n$n -s$scenario -d$duration);
    pow=$(echo "$r" | awk '/Total power/ { print $6 }');
    latw=$(echo "$r" | awk '/ECDF-wake/ { print $5 }');
    lats=$(echo "$r" | awk '/ECDF-sleep/ { print $5 }');
    printf " %-10.2f %-10d %-10d\n" $pow $latw $lats;
}

for p in $points;
do
    printf "%-10d " $p
    for n in $cores;
    do
	repeat_med $reps 1 do_run;
    done;
    echo "";
done;
