#!/bin/bash

source ./scripts/config;

duration=10000;

executable=futex;

points="$(seq 0 1000 9000)";
points="$points $(seq 10000 10000 90000)";
points="$points $(seq 100000 100000 900000)";
points="$points $(seq 1000000 1000000 9000000)";
points="$points $(seq 10000000 10000000 10000000)";

printf "#%-9s %-8s %-8s %-8s %-8s %-8s %-8s\n" "before" "wk-med" "wk-5" "wk-95" "hand-med" "hand-5" "hand-95";

for p in $points
do
    printf "%-10d " $p;
    r=$(./$executable $@ -s1 -c$p -d$duration);
    wake_med=$(echo "$r" | awk '/ECDF-wake/ { print $5 }');
    wake_5=$(echo "$r" | awk '/ECDF-wake/ { print $3 }');
    wake_95=$(echo "$r" | awk '/ECDF-wake/ { print $7 }');
    handover_med=$(echo "$r" | awk '/ECDF-handover/ { print $5 }');
    handover_5=$(echo "$r" | awk '/ECDF-handover/ { print $3 }');
    handover_95=$(echo "$r" | awk '/ECDF-handover/ { print $7 }');

    printf "%-8d %-8d %-8d %-8d %-8d %-8d \n" $wake_med $wake_5 $wake_95 $handover_med $handover_5 $handover_95;
done;

