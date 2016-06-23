#!/bin/bash


if [ ! $# -eq 3 ];
then
    echo "** Need 3 args: ON/OFF (0 or 1), start_core_id, end_core_id";
exit;
fi;

on_or_off=$1;
shift
from=$1;
shift;
to=$1;
shift;

printf "** Will set the online status of cores $from - $to to $on_or_off. (y/N)? ";
yn=n;
read yn;
if [ "$yn"0 = "y0" ];
then
    for c in $(seq $from 1 $to)
    do
	printf "* Core ${c} to ";
	echo $on_or_off | sudo tee /sys/devices/system/cpu/cpu${c}/online
    done;
fi;
