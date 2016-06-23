#!/bin/bash

repetitions=2;
cores="$(seq 1 1 8)";

if [ $UNAMEN = "lpdpc4" ];
then
    LOCKS="USE_SPINLOCK_LOCKS USE_TTAS_LOCKS USE_TICKET_LOCKS USE_MCS_LOCKS USE_CLH_LOCKS USE_ARRAY_LOCKS USE_MUTEX_LOCKS";
fi;

if [ $UNAMEN = "lpdpc34" ];
then
    LOCKS="USE_SPINLOCK_LOCKS USE_TTAS_LOCKS USE_TICKET_LOCKS USE_MCS_LOCKS USE_CLH_LOCKS USE_ARRAY_LOCKS USE_MUTEX_LOCKS";
fi;

prefix="stress_test_"
tmp_file="energy_all_locks.tmp";

echo "#params: $@";
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
	    ./scripts/run_power_measurements.sh $executable $@ -n$c >>  $tmp_file;
	done;

	res=$(awk -f ./scripts/avg.awk $tmp_file;);
	thr=$(echo $res | awk '// { print $2 }');
	ppw=$(echo $res | awk '// { print $6 }');
	energy_per_op=$(echo $res | awk '// { print $5 }');
	printf "%-10.0f%-10.6f%-10.0f" $thr $energy_per_op $ppw;
    done;
    echo "";
done;

rm $tmp_file;
