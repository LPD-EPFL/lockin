#!/bin/bash

repetitions=1;

UNAMEN=$(uname -n);
if [ $UNAMEN = "lpdpc4" ];
then
    cores="$(seq 1 1 8)";
    LOCKS="USE_SPINLOCK_LOCKS USE_TTAS_LOCKS USE_TICKET_LOCKS USE_MCS_LOCKS USE_CLH_LOCKS USE_ARRAY_LOCKS USE_MUTEX_LOCKS";
fi;

if [ $UNAMEN = "lpdpc34" ];
then
    cores="$(seq 1 1 8)";
    LOCKS="USE_SPINLOCK_LOCKS USE_TTAS_LOCKS USE_TICKET_LOCKS USE_MCS_LOCKS USE_CLH_LOCKS USE_ARRAY_LOCKS USE_MUTEX_LOCKS";
fi;

if [ $UNAMEN = "lpdxeon2680" ];
then
    cores="$(seq 1 1 40)";
    LOCKS="USE_SPINLOCK_LOCKS USE_TTAS_LOCKS USE_TICKET_LOCKS USE_MCS_LOCKS USE_CLH_LOCKS USE_HTICKET_LOCKS USE_MUTEX_LOCKS";
fi;

prefix="stress_one_"
tmp_file="energy_all_locks.tmp";

echo "#params: $@";
echo "#output: throughput   throughput/Watt     uJ/operation";
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
	    ./scripts/run_power_measurements_rapl.sh $executable $@ -n$c >>  $tmp_file;
	done;

	res=$(awk -f ./scripts/avg.awk $tmp_file;);
	thr=$(echo $res | awk '// { print $1 }');
	ppw=$(echo $res | awk '// { print $3 }');
	energy_per_op=$(echo $res | awk '// { print $4 }');
	printf "%-10.0f%-10.6f%-10.0f" $thr $energy_per_op $ppw;
    done;
    echo "";
done;

rm $tmp_file;
