#!/bin/bash

n=$1;
shift;

n1=$(($n - 1));

res=$(./pc -c0-$n1 -g BRANCH ./$@ -n$n);
# echo "$res";

if [ $n1 -eq 0 ];
then
    CPI=$(echo "$res" | awk '/CPI/ { print $4 }');
else
    CPI=$(echo "$res" | awk '/CPI STAT/ { print $11 }');
fi;

 printf "%-10.2f\n" $CPI




