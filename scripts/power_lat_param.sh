#!/bin/bash

param_flag=$1;
shift;
params_var=$1;
shift;

reps=$1;
shift;

source scripts/config;
source scripts/help;
source scripts/sudo;

result_type=$1;

if [ "$result_type" = "max" ];
then
    run_script="./scripts/run_rep_pow_lat_max.sh $reps";
    echo "# Result from $reps repetitions: max";
    shift;

elif [ "$result_type" = "min" ];
then
    # run_script="./scripts/run_rep_pow_lat_min.sh $reps";
    # echo "# Result from $reps repetitions: min";
    echo "** min not implemented";
    exit;
    shift;
elif [ "$result_type" = "median" ];
then
    run_script="./scripts/run_rep_pow_lat_med.sh $reps";
    echo "# Result from $reps repetitions: median";
    shift;
else
    run_script="./scripts/run_rep_pow_lat_max.sh $reps";
    echo "# Result from $reps repetitions: max (default). Available: min, max, median";
fi;

run_script="sudo $run_script";

progs="$1";
shift;
progs_num=$(echo $progs | wc -w);
params="$@";

print_n "#       " "%-66s" "$progs" "\n"

print_rep "#co     " $progs_num "Thrput     Thrput/W     Power      uJ/op        lat_lock lat_ulck " "\n";

for param in $params_var
do
    printf "%-8d" $param;
    for p in $progs;
    do
	res=$($run_script ./$p $params -${param_flag}${param});
	printf "%-11d%-12.2f%-12.6f%-13.6f%-9d%-9d" $res
    done;     
    echo "";
done;

