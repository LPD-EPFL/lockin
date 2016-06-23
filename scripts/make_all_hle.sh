#!/bin/bash

hle_types=( no hle rtm_pes rtm_opt );
locks=( TAS TTAS );

for l in ${locks[@]};
do
    echo "** building: $l";    
    for i in $(seq 0 1 3);
    do
	echo "** with: ${hle_types[$i]}";
	target=stress_one_in;
	make LOCK_IN=$l RMT=1 USE_HLE=$i ${target};
	mv ${target} ${target}_${l}_${hle_types[$i]};

	target=stress_test_in;
	make LOCK_IN=$l RTM=1 USE_HLE=$i ${target};
	mv ${target} ${target}_${l}_${hle_types[$i]};
    done;
done;