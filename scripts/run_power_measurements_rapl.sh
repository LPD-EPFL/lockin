#!/bin/bash

res=$(sudo ./$@);
# echo "$res";
pow=$(echo "$res" | awk '/0 power/ { print $6 }');
pow_pac=$(echo "$res" | awk '/Package power/ { print $6 }');
pow_tot=$(echo "$res" | awk '/Total power/ { print $6 }');
thr=$(echo "$res" | awk '/#acquires/ { print $5 }');
ppw=$(echo "$res" | awk '/#ppw/ { print $6 }');
eop=$(echo "$res" | awk '/#eop/ { print $6 }');

printf "%-10d %-10f %-10.0f %10f\n" $thr $pow_tot $ppw $eop;
