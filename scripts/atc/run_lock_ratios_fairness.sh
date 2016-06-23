#!/bin/bash

cores=over;
duration=5000;
reps=11;
set_cpu=0;
median_column=4;		# 99.99% latency

params="-s${set_cpu}";

. ./scripts/config;
. ./scripts/help;
. ./scripts/repeat_med.sh
#points=$(seq 0 1000 84000);
points="$(seq 0 500 32000)";
# max=$(echo "2^20" | bc)
# s=128;
# while [ $s -le $max ];
# do
#     points="$points $s";
#     s=$((s*2));
# done;

eprefix=./stress_ldi_in_;
executables=( MUTEXEE MUTEXEEF );
en=${#executables[@]};

c_num=$(echo "1 "${cores} | wc -w);
p_num=$(echo ${points} | wc -w);
time=$(echo "($duration/1000)*${p_num}*${reps}*${c_num}*${en}/3600" | bc -l);
printf "#The experiment will take about: %.2f hours\n" $time;

# skip=$#;
# if [ $skip -eq 0 ];
# then
#     printf "#   Continue? [Y/n] ";
#     read cont;
#     if [ "$cont" = "n" ];
#     then
# 	exit;
#     fi;
# fi;

function do_run()
{
    ex=${executables[$e]};
    r=$(${eprefix}${ex} $@ $params -a${p} -n${c} -d${duration});
    thr=$(echo "$r" | awk '/acquires/ { print $5 }');
    eop=$(echo "$r" | awk '/eop/ { print $4 }');
    l99=$(echo "$r" | awk '/ECDF-lock/ { print $7 }');
    lmax=$(echo "$r" | awk '/ECDF-lock/ { print $8 }');
    printf "%-10d %-9.3f %-10d %-11d\n" $thr $eop $l99 $lmax;
}

for p in $points;
do
    echo "#### -c $p";
    print_arr_n "#Core       " "%-39s " executables[@] ""
    printf "   %-8s %-8s\n" "THR" "EOP"
    for c in $cores;
    do
    	printf "#R %-5d %-2d " $p $c;
    	for ((e = 0; e < $en; e++))
    	do
	    res=($(repeat_med $reps $median_column do_run));
	    thr=${res[0]};
	    eop=${res[1]};
	    printf "%-9d %-8.2f %-9d %-10d " ${res[@]};
 	    thrs[$e]=$thr;
	    eops[$e]=$eop;
    	done;

	thr_max_idx=$(echo ${thrs[@]} | awk '{max=0; mi=0; for(i=1;i<=NF;i++){if($i>max) { max = $i; mi=i }};} END { print mi-1 }');
	eop_min_idx=$(echo ${eops[@]} | awk '{min=1e9; mi=0; for(i=1;i<=NF;i++){if($i<min) { min = $i; mi=i }};} END { print mi-1 }');
	printf "   %-8s %-8s\n" ${executables[$thr_max_idx]} ${executables[$eop_min_idx]};
	for ((i = 0; i < $en; i++))
	do
	    for ((j = $i + 1; j < $en; j++))
	    do
		idx_offset=$(($i+(100*$j)+$c));
		tr[$idx_offset]=$(echo ${thrs[$j]}/${thrs[$i]} | bc -l);
		eop_neg=$(echo "${eops[$i]} < 0 || ${eops[$j]} < 0" | bc);
		if [ "${eop_neg}a" = "0a" ];
		then
		    er[$idx_offset]=$(echo ${eops[$i]}/${eops[$j]} | bc -l);
		else
		    er[$idx_offset]=$tr;
		fi;
	    done
	done
    done

    for ((i = 0; i < $en; i++))
    do
	for ((j = $i + 1; j < $en; j++))
	do
	    thr_str="";
	    eop_str="";
	    for c in $cores;
	    do
		idx_offset=$(($i+(100*$j)+$c));
		thr_str="$thr_str $(printf "%-6.3f" ${tr[$idx_offset]})";
		eop_str="$eop_str $(printf "%-6.3f" ${er[$idx_offset]})";
	    done
	    printf "THR:%-15s %-10d %s\n" "${executables[$j]}/${executables[$i]}" $p "$thr_str"
	    printf "EOP:%-15s %-10d %s\n" "${executables[$j]}/${executables[$i]}" $p "$eop_str"
	done
    done
done


