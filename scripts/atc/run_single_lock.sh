#!/bin/bash


source scripts/config;
source scripts/help;

locks=( $LOCKS_PLUS );
test_dur=5000;
repetitions=3;
cs_dur=1000;
# fair_dur=100;
cores=allover;
set_cpu=0;

executable=stress_one_in
params="-d$test_dur -a$cs_dur -s$set_cpu";
executables="${locks[@]/#/${executable}_}"


echo "# testing: $executables";
echo "# params : $params";

./scripts/power_simple.sh $cores $repetitions median "$executables" $params;
