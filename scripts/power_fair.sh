#!/bin/bash
cores=$1;
shift;

reps=$1;
shift;

source scripts/config;
source scripts/help;

result_type=$1;

if [ "$result_type" = "max" ];
then
    run_script="./scripts/run_rep_pow_fair_max.sh $reps";
    echo "# Result from $reps repetitions: max";
    shift;

elif [ "$result_type" = "min" ];
then
    # run_script="./scripts/run_rep_pow_min.sh $reps";
    # echo "# Result from $reps repetitions: min";
    echo "** min not implemented";
    shift;
elif [ "$result_type" = "median" ];
then
    # run_script="./scripts/run_rep_pow_med.sh $reps";
    # echo "# Result from $reps repetitions: median";
    echo "** median not implemented";
    shift;
else
    run_script="./scripts/run_rep_pow_fair_max.sh $reps";
    echo "# Result from $reps repetitions: max (default). Available: min, max, median";
fi;

run_script="sudo $run_script";

progs="$1";
shift;
progs_num=$(echo $progs | wc -w);
params="$@";

print_n "#   " "%-46s" "$progs" "\n"
                                        
print_rep "#co " $progs_num "Thrput      Consecu     Power      uJ/op        " "\n";

d=0;
for c in 1 $cores
do
    if [ $c -eq 1 ];
    then
	if [ $d -eq 1 ];
	then
	    continue;
	fi;
    fi;
    d=1;

    printf "%-4d" $c;
    i=0;
    for p in $progs;
    do
	i=$(($i+1));
	res=$($run_script ./$p $params -n$c);
	printf "%-11d%-12.2f%-12.6f%-13.6f" $res
    done;     
    echo "";
done;

