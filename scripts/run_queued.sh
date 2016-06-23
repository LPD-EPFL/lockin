#!/bin/bash

cores=$1;
shift;
executables="$1";
shift

source ./scripts/config;
source ./scripts/sudo;

repetitions=1;

executable=stress_queued_in;

tmp_file="energy_all_locks.tmp";

echo "#CPI / Package power / Total power   // params: $@";
printf "#Cor ";
for lock in $executables
do
    printf "%-30s" $(echo $lock | sed "s/${executable}_//g");
done;


echo "";

for c in 1 $cores
do
    printf "%-5d" $c;

    for executable in $executables
    do
	printf "" > $tmp_file;
	for r in $(seq 1 1 $repetitions)
	do
	    ./scripts/run_cpi_pow_measurements_rapl.sh $c $executable $@ >>  $tmp_file;
	done;

	res=$(awk -f ./scripts/avg.awk $tmp_file | awk '// { print $1,$2,$3 }');
	printf "%-10.2f%-10.2f%-10.4f" $res;
	
    done;
    echo "";
done;
