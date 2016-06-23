#!/bin/bash

repetitions=1;

source scripts/config;

prefix="stress_queued_"
tmp_file="energy_all_locks.tmp";

echo "#params: $@";
echo "#Output: CPI    Energy(J)    Power"
printf "#Cor ";
for lock in $LOCKS
do
    suffix=`echo $lock | sed -e "s/USE_//g" -e "s/_LOCK\?//g" | tr "[:upper:]" "[:lower:]"`;
    printf "%-36s" $suffix;
done;

echo "";

for c in $cores
do
    printf "%-5d" $c;

    for lock in $LOCKS
    do
	suffix=`echo $lock | sed -e "s/USE_//g" -e "s/_LOCK\?//g" | tr "[:upper:]" "[:lower:]"`;
	executable="$prefix$suffix";

	printf "" > $tmp_file;
	for r in $(seq 1 1 $repetitions)
	do
	    ./scripts/run_cpi_measurements_rapl.sh $c $executable $@ >>  $tmp_file;
	done;

	res=$(awk -f ./scripts/avg.awk $tmp_file | awk '// { print $1,$2,$3 }');
	printf "%-10.2f %-12f %-12f" $res;
	
    done;
    echo "";
done;

# rm $tmp_file;
