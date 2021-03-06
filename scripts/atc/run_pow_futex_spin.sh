#!/bin/bash

. ./scripts/repeat_med.sh

reps=11;
duration=5000;
scenarios="1 7";

points="0";
max=$(echo "2^25" | bc)
s=1;
while [ $s -le $max ];
do
    points="$points $s";
    s=$((s*2));
done;

cores="2"

# printf "%-11s %-21d %-21d %-21d %-21d \n" "#delay" $cores;
printf "%-11s -s%-19d -s%-19d \n" "#delay" ${scenarios[@]};

function do_run()
{
    r=$(./futex_pow_only $@ -c$p -n$n -s$scenario -d$duration);
    pow=$(echo "$r" | awk '/Total power/ { print $6 }');
    # lat=$(echo "$r" | awk '/ECDF-wake/ { print $5 }');
    printf " %-10.2f %-10d\n" $pow 0;
}

for p in $points;
do
    printf "%-10d " $p
    for n in $cores;
    do
	for scenario in $scenarios;
	do
	    repeat_med $reps 1 do_run;
	done
    done;
    echo "";
done;
