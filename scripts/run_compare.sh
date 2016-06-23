#!/bin/bash

cores=socket;

. ./scripts/config;
. ./scripts/help;
points=$(seq 0 1000 128000);
# max=$(echo "2^20" | bc)
# s=128;
# while [ $s -le $max ];
# do
#     points="$points $s";
#     s=$((s*2));
# done;

eprefix=./stress_one_in_;
executables=( MUTEX TTAS TICKET MUTEXEE );
en=${#executables[@]};


for p in $points;
do
    echo "#### -c $p";
    print_arr_n "#Core " "%-20s " executables[@] ""
    printf "   %-10s %-10s\n" "THR" "EOP"
    for c in $cores;
    do
    	printf "## %-2d " $c;
    	for ((e = 0; e < $en; e++))
    	do
    	    ex=${executables[$e]};
    	    r=$(${eprefix}${ex} $@ -a${p} -n${c});
    	    thr=$(echo "$r" | awk '/acquires/ { print $5 }');
    	    eop=$(echo "$r" | awk '/eop/ { print $4 }');
    	    printf "%-10d %-9.3f " $thr $eop;
	    thrs[$e]=$thr;
	    eops[$e]=$eop;
    	done;

	thr_max_idx=$(echo ${thrs[@]} | awk '{max=0; mi=0; for(i=1;i<=NF;i++){if($i>max) { max = $i; mi=i }};} END { print mi-1 }');
	eop_min_idx=$(echo ${eops[@]} | awk '{min=1e9; mi=0; for(i=1;i<=NF;i++){if($i<min) { min = $i; mi=i }};} END { print mi-1 }');
	
    printf "   %-10s %-10s\n" ${executables[$thr_max_idx]} ${executables[$eop_min_idx]};
    done
done


