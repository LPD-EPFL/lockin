#!/bin/bash

reps=$1;
shift;

for r in $(seq 1 1 $reps);
do
    # echo "#Repetition             : $r";
    $@ | tail -n1;
done
