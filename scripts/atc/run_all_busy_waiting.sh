#!/bin/bash

sf=./scripts/atc
df=./data

echo "## Please compile with different busy-waiting tenchniques using";
echo "## ./scripts/make_queued_pause_types.sh "

read y;

echo "##RUN Cost of spinning";

name=run_cost_waiting;
${sf}/${name}.sh | tee -a ${df}/${name}.dat

echo "##RUN Cost of spinning - reducing";

name=run_cost_spinning;
${sf}/${name}.sh | tee -a ${df}/${name}.dat

echo "##RUN Cost of spinning - DVFS";

name=run_cost_spinning_dvfs;
${sf}/${name}.sh | tee -a ${df}/${name}.dat

echo "##RUN Cost of spinning - mwait";

name=run_cost_spinning_mwait;
${sf}/${name}.sh | tee -a ${df}/${name}.dat
