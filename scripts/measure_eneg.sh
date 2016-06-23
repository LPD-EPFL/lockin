#!/bin/bash

echo "** Please give me root access!"
sudo printf "";

duration=10;			# duration of each measurement
interval=12;			# interval between each measurement
first=4;			# when to start the 1st
sockets=2;
measurements=6;			# number of measurements

sudo printf "" > /tmp/pow.tmp;

for m in $(seq 0 1 $(($measurements-1)));
do
    start=$(($first + $m*$interval));
    echo "measurement $m :: starting at second $start";
    sudo ./energy_sock $duration $start $sockets >> /tmp/pow.tmp &
done;

sleep $((${measurements}*${interval} + ${first} + ${duration} + 2)); # there you have your execution

pow=$(cat /tmp/pow.tmp | awk '/Total power/ { print $6 " (" $7 ", " $8")" }';);

echo "$pow";
