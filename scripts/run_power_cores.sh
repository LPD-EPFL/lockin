#!/bin/bash

cores="$1";
shift;

printf "#Cores Thput(O/s)     Power(W) Energy(J) Enrg/Op(mj/o) Perf/Pow(thr/w)\n";
for c in $cores
do
    printf "%-6d " $c;
    ./scripts/run_power_measurements_rapl.sh $@ -n$c;
done;
