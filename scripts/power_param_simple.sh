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
    run_script="./scripts/run_rep_pow_max.sh $reps";
    echo "# Result from $reps repetitions: max";
    shift;

elif [ "$result_type" = "min" ];
then
    run_script="./scripts/run_rep_pow_min.sh $reps";
    echo "# Result from $reps repetitions: min";
    shift;
elif [ "$result_type" = "median" ];
then
    run_script="./scripts/run_rep_pow_med.sh $reps";
    echo "# Result from $reps repetitions: median";
    shift;
else
    run_script="./scripts/run_rep_pow_max.sh $reps";
    echo "# Result from $reps repetitions: max (default). Available: min, max, median";
fi;

run_script="sudo $run_script";

progs="$1";
shift;
progs_num=$(echo $progs | wc -w);
params="$@";

print_n   "#       " "%-36s" "$progs" "\n"

print_rep "#pr     " $progs_num "Thrput     Power       uJ/op        " "\n";

for param in $params_var
do
    printf "%-8d" $param;
    for p in $progs;
    do
	res=$($run_script ./$p $params -${param_flag}${param} | awk '// { $2=""; print; }');
	printf "%-11d%-12.2f%-13.6f" $res
    done;     
    echo "";
done;

