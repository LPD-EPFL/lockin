#!/bin/bash

source power/config/config;

reps=$1;
shift;

keep_n=1;

rep_script=./scripts/repeat_and_keep_lat.sh

while [ 1 ];
do
    out=$($rep_script $reps $@);
    vstat=$(./vstats $out);

    vstata=($vstat);
    std=${vstata[4]};
    stdhigh=$(echo "$std>=$stdmax" | bc -l);
    if [ $stdhigh -eq 1 ];
    then
	echo "--high std ($std), repeat";
	stdmax=$(echo "$stdmax+$stdstep" | bc -l);
    else
	break;
    fi;
done;

echo "$vstat ("$out")";
