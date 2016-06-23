#!/bin/bash

cores=all;

source ./scripts/config;
source ./scripts/sudo;
source ./scripts/repeat_med.sh;

repetitions=7;
duration=5000;

pr=stress_queued_in;
executables="${pr}_MUTEX_empty ${pr}_TAS_empty ${pr}_TTAS_empty ${pr}_TTAS_nop ${pr}_TTAS_pause ${pr}_TTAS_mfence";

tmp_file="/tmp/energy_all_locks.tmp";

echo "#CPI / Package power / Total power   // params: $@";
printf "#Cor ";
for lock in $executables
do
    suffix=`echo $lock | sed -e "s/USE_//g" -e "s/_LOCK\?//g" | tr "[:upper:]" "[:lower:]"`;
    printf "%-30s" $suffix;
done;

echo "";

function do_run()
{
    ./scripts/run_cpi_pow_measurements_rapl.sh $c $executable -d$duration $@;
}


for c in 1 $cores
do
    printf "%-5d" $c;

    for executable in $executables
    do
	# printf "" > $tmp_file;
	# for r in $(seq 1 1 $repetitions)
	# do
	#     ./scripts/run_cpi_pow_measurements_rapl.sh $c $executable -d$duration $@ >>  $tmp_file;
	# done;

	# res=$(cat "$tmp_file" | grep -v "\-" | awk -f ./scripts/avg.awk | awk '// { print $1,$2,$3 }');
	res=$(repeat_med $repetitions 3 do_run);
	printf "%-10.2f%-10.2f%-10.4f" $res;
	
    done;
    echo "";
done;

# rm $tmp_file;
