#!/bin/bash

source power/config/config;

reps=$1;
shift;
keep="$1";
shift;

keep_n=3;
rep_script=./scripts/repeat_and_keep.sh
pfiles=( /tmp/Tot /tmp/Pac /tmp/PP0 );

while [ 1 ];
do
    out=$($rep_script $reps $keep $keep_n $@);
    vstat=$(./vstats $out);
    # echo ${vstat[@]};
    vstata=($vstat);
    std=${vstata[4]};
    stdhigh=$(echo "$std>=$stdmax" | bc -l);
    if [ $stdhigh -eq 1 ];
    then
	echo "--high std ($std), repeat";
	stdmax=$(echo "$stdmax+$stdstep" | bc -l);
    else
	# calc all averages
	for f in ${pfiles[@]};
	do
	    data=$(cat $f);
	    vstatf=$(./vstats $data);
	    echo $vstatf > $f;
	done;
	break;
    fi;
done;

echo "$vstat ("$out")";
