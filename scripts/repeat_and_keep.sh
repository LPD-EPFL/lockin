#!/bin/bash

reps=$1;
shift;
keep="$1";
shift;
keep_n=$1;
shift;

pfiles=( /tmp/Tot /tmp/Pac /tmp/PP0 );
for f in ${pfiles[@]};
do
    printf "" > $f;
done;
	 
for r in $(seq 1 1 $reps);
do
    out=$($@);
    all=($(echo "$out" | tail -n 3 | awk "// { print \$$keep_n }"));
    for (( i = 0; i < ${#all[@]}; i++ ));
    do
	echo ${all[$i]} >> ${pfiles[$i]};
    done;
    
    echo "$out" | awk "/$keep/ { print \$$keep_n }";
done
