#!/bin/bash

for i in $(seq 0 1 $1);
do
    cpufreq-set -c$i -f$2;
done
