#!/bin/bash

cores=allbutone;

source ./scripts/config;
source ./scripts/sudo;

repetitions=7;
duration=5000;

executables=( stress_mwait_queued );

tmp_file="/tmp/energy_all_locks.tmp";

echo "#CPI / Package power / Total power   // params: $@";
printf "#Cor ";
for lock in ${executables[@]}
do
    suffix=`echo $lock | sed -e "s/USE_//g" -e "s/_LOCK\?//g" | tr "[:upper:]" "[:lower:]"`;
    printf "%-30s" $suffix;
done;

echo "";

for c in 1 $cores
do
    printf "%-5d" $c;

    for (( i = 0; i < ${#executables[@]}; i++ ))
    do
	executable=${executables[$i]};
	flag=${flags[$i]};
	printf "" > $tmp_file;
	for r in $(seq 1 1 $repetitions)
	do
	    ./scripts/run_cpi_pow_measurements_rapl.sh $c $executable -d$duration $@ $flag >>  $tmp_file;
	done;

	res=$(cat "$tmp_file" | grep -v "\-" | awk -f ./scripts/avg.awk | awk '// { print $1,$2,$3 }');
	printf "%-10.2f%-10.2f%-10.4f" $res;
	
    done;
    echo "";
done;

# rm $tmp_file;
