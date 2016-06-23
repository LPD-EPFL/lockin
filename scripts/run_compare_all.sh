#!/bin/bash

cores="1 2 4 8 16";
cores="3 5 6 7 9 10 11 12 13 14 15";
duration=1000;
reps=3;
set_cpu=1;
. ./scripts/config;
. ./scripts/help;
. ./scripts/repeat_med.sh

cs_dur="$(seq 0 500 4000) $(seq 5000 1000 8000)";
nlocks="1 2 4 8 16 32 64 128 256 512";

eprefix=./stress_test_in_;
executables=( MUTEX TAS TTAS TICKET MCS MUTEXEE );
en=${#executables[@]};

c_num=$(echo "1 "${cores} | wc -w);
p_num=$(echo ${cs_dur} | wc -w);
l_num=$(echo ${nlocks} | wc -w);
time=$(echo "($duration/1000)*${p_num}*${l_num}*${reps}*${c_num}*${en}/3600" | bc -l);
printf "#The experiment will take about: %.2f hours\n" $time;

skip=$#;
if [ $skip -eq 0 ];
then
    printf "#   Continue? [Y/n] ";
    read cont;
    if [ "$cont" = "n" ];
    then
	exit;
    fi;
fi;

function do_run()
{
    ex=${executables[$e]};
    r=$(${eprefix}${ex} $@ -l${nl} -a${cs} -n${c} -d${duration} -s${set_cpu});
    thr=$(echo "$r" | awk '/acquires/ { print $5 }');
    ppw=$(echo "$r" | awk '/ppw/ { print $4 }');
    printf "%-10d %-9.3f " $thr $ppw;
}

print_arr_n "#Co #nl   #cs    " "%-20s " executables[@] ""
printf "   %-10s %-10s\n" "THR" "PPW"

for c in $cores;
do
    for nl in $nlocks
    do
	for cs in $cs_dur;
	do
    	    printf "%-3d %-5d %-6d " $c $nl $cs;
    	    for ((e = 0; e < $en; e++))
    	    do
		res=($(repeat_med $reps 1 do_run));
		thr=${res[0]};
		ppw=${res[1]};
		printf "%-10d %-10.0f" $thr $ppw;
 		thrs[$e]=$thr;
		ppws[$e]=$ppw;
    	    done;

	    thr_max_idx=$(echo ${thrs[@]} | awk '{max=0; mi=0; for(i=1;i<=NF;i++){if($i>max) { max = $i; mi=i }};} END { print mi-1 }');
	    ppw_max_idx=$(echo ${ppws[@]} | awk '{max=0; mi=0; for(i=1;i<=NF;i++){if($i>max) { max = $i; mi=i }};} END { print mi-1 }');
	    thr_ratio=$(echo ${thrs[$thr_max_idx]}/${thrs[$ppw_max_idx]} | bc -l);
	    ppw_ratio=$(echo ${ppws[$ppw_max_idx]}/${ppws[$thr_max_idx]} | bc -l);
	    printf "   %-10s %-10s %-4.2f %-4.2f\n" ${executables[$thr_max_idx]} ${executables[$ppw_max_idx]} $thr_ratio $ppw_ratio;
	done
    done
done

