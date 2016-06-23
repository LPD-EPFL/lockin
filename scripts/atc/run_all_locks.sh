#!/bin/bash

sf=./scripts/atc
df=./data

echo "## Please compile the stress tests with ./scripts/make_all_stress_tests.sh ";

names="run_single_thread_multi_locks run_single_lock run_vary_nlocks run_lock_ratios_fairness";

un=$(uname -n);

if [ $un = lpdpc34 ];
then
    data_prefix=$un;
fi;

for name in $names;
do
    echo "##RUN $name";
    ${sf}/${name}.sh | tee ${df}/${name}.${data_prefix}.dat
done

