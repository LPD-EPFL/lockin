#!/bin/bash


while [ 1 ];
do
    pow=$(sudo energy_sock 1 0 2 | awk '/Total power/ { $1=""; $2=""; print; }');
    echo $pow;
done;
