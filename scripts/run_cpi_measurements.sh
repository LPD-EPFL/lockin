#!/bin/bash

n=$1;
shift;

n1=$(($n - 1));

# printf "%-10s%-10s%-10s\n" "#CPI" "Energy" "Power";

res=$(sudo likwid-perfctr -c0-$n1 -g ENERGY ./$@ -n$n);
echo "$res";

if [ $n1 -eq 0 ];
then
    CPI=$(echo "$res" | awk '/CPI/ { print $4 }');
    eng=$(echo "$res" | awk '/Energy/ { print $5 }');
    pow=$(echo "$res" | awk '/Power/ { print $5 }');
else
    CPI=$(echo "$res" | awk '/CPI STAT/ { print $11 }');
    eng=$(echo "$res" | awk '/Energy.*STAT/ { print $6 }');
    pow=$(echo "$res" | awk '/Power.*STAT/ { print $6 }');
fi;

# echo "CPI    : $CPI";
# echo "Energy : $eng";
# echo "Power  : $pow";
printf "%-10.2f%-10.2f%-10.4f\n" $CPI $eng $pow;



