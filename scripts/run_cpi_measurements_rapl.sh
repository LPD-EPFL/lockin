#!/bin/bash

n=$1;
shift;

n1=$(($n - 1));

# printf "%-10s%-10s%-10s\n" "#CPI" "Energy" "Power";

res=$(sudo likwid-perfctr -M0 -c0-$n1 -g ENERGY ./$@ -n$n);
# echo "$res";

if [ $n1 -eq 0 ];
then
    CPI=$(echo "$res" | awk '/CPI/ { print $4 }');
else
    CPI=$(echo "$res" | awk '/CPI STAT/ { print $11 }');
fi;

pow=$(echo "$res" | awk '/0 power/ { print $6 }');
pow_pac=$(echo "$res" | awk '/Package power/ { print $6 }');
pow_tot=$(echo "$res" | awk '/Total power/ { print $6 }');
eng=$(echo "$res" | awk '/0 energy/ { print $6 }');

# echo "CPI    : $CPI";
# echo "Energy : $eng";
# echo "Power  : $pow";
# echo "Power P: $pow_pac";
# echo "Power T: $pow_tot";
printf "%-10.2f %-12f %-12f\n" $CPI $eng $pow_tot;



