#!/bin/bash


function repeat_med()
{
    un=$(uname -n);
    tmp=/tmp/repeat_med.${un}.tmp
    reps=$1;
    shift;
    med_base=$1; ## column-based on which to keep
    shift;
    exect=$1;

    while [ 1 ];
    do
	printf '' > $tmp;

	for ((i = 0; i < $reps; i++))
	do
	    $exect >> $tmp;
	done

	nm=$(cat $tmp | grep -v "-" | wc -l);
	med_idx=$(echo "1 + $nm/2" | bc);
	out=$(sort -n -k$med_base $tmp | grep -v "-" | head -${med_idx} | tail -n1);
	if [ $nm -gt 0 ];
	then
	    printf "$out";
	    break;
	fi
    done
}


