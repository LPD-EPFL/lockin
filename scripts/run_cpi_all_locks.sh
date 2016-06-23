#!/bin/bash

repetitions=3;
cores="$(seq 1 1 8)";

LOCKS="USE_SPINLOCK_LOCKS USE_TTAS_LOCKS USE_TICKET_LOCKS USE_MCS_LOCKS USE_CLH_LOCKS USE_ARRAY_LOCKS USE_HCLH_LOCKS USE_HTICKET_LOCKS USE_MUTEX_LOCKS";

prefix="stress_queued_"
tmp_file="energy_all_locks.tmp";

echo "#CPI / Energy / Power   // params: $@";
printf "#Cor ";
for lock in $LOCKS
do
    suffix=`echo $lock | sed -e "s/USE_//g" -e "s/_LOCK\?//g" | tr "[:upper:]" "[:lower:]"`;
    printf "%-30s" $suffix;
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
	    ./scripts/run_cpi_measurements.sh $c $executable $@ >>  $tmp_file;
	done;

	res=$(awk -f ./scripts/avg.awk $tmp_file | awk '// { print $1,$2,$3 }');
	printf "%-10.2f%-10.2f%-10.4f" $res;
	
    done;
    echo "";
done;

# rm $tmp_file;
