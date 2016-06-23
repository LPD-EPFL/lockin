#!/bin/bash

sf=./scripts/atc
df=./data

echo "## Please compile futex";

read y;

echo "##RUN Latency of futex";

name=run_latency_futex;
${sf}/${name}.sh | tee -a ${df}/${name}.dat

echo "##RUN Power sleeping vs. spinning";

name=run_pow_futex_spin;
${sf}/${name}.sh | tee -a ${df}/${name}.dat

echo "##RUN Spin then sleep";

name=run_spin_then_sleep;
${sf}/${name}.sh | tee -a ${df}/${name}.dat


