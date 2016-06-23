#!/bin/bash

source scripts/config;
source scripts/help;

locks=( $LOCKS_PLUS );
num_cores=8;
param=l;
num_params=16;
test_dur=2000;
repetitions=3;
cs_dur=1000;
# fair_dur=100;
set_cpu=1;

do_params="";
p2=1;
for (( i=0; i < $num_params; i++ ))
do
    do_params="$do_params $p2";
    p2=$((2*$p2));
done;

executable=stress_test_in
params="-d$test_dur -a$cs_dur -s$set_cpu -n$num_cores";
executables="${locks[@]/#/${executable}_}"


echo "# testing: $executables";
echo "# params : $params / on -$param $do_params";

./scripts/power_param.sh $param "$do_params" $repetitions median "$executables" $params;
