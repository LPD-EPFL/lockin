#!/bin/bash

. scripts/sudo;
. scripts/config;
. scripts/help;

locks=( $LOCKS_PLUS );
num_cores=1;
param=l;
num_params=16;
first_param=1;
test_dur=2000;
repetitions=3;
cs_dur=100;
fair_dur=0;

do_params="";
p2=$first_param;
for (( i=0; i < $num_params; i++ ))
do
    do_params="$do_params $p2";
    p2=$((2*$p2));
done;

executable=stress_test_in
params="-d$test_dur -f$fair_dur -n$num_cores -a$cs_dur";
executables="${locks[@]/#/${executable}_}"


echo "# testing: $executables";
echo "# params : $params / on -$param $do_params";

./scripts/power_param.sh $param "$do_params" $repetitions median2 "$executables" $params;
