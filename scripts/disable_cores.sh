#!/bin/bash

if [ $# -lt 3 ];
then
    echo "Usage: $0 from_id to_id y/n";
    echo "       y to enable cores, n to disable";
    exit;
fi;

from=$1;
shift;
to=$1;
shift;
yn=$1;
shift;

printf "Handling: "

for c in $(seq $from 1 $to);
do
    printf "%d " $c;
    sudo sh -c "echo '${yn}' > /sys/devices/system/cpu/cpu${c}/online"
done;

echo "";
