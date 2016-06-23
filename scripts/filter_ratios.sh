#!/bin/bash

file=$1;
shift

if [ $# -eq 3 ];
then
    keep=$1;
    shift;
    l1=$1;
    shift;
    l2=$1;
    shift;

    echo "## ${keep}";
    cat $file | awk "/${keep}:$l1\/$l2/ { \$1=\" \"; \$2=\" \"; print; }";
else
    executables=( MUTEX TTAS MUTEXEE );
    executables=( MUTEX UPMUTEX2 UPMUTEX4 );
    en=${#executables[@]};

    for e in THR EOP;
    do
	for ((i = 0; i < $en; i++))
	do
	    for ((j = $i + 1; j < $en; j++))
	    do
		l1=${executables[$j]};
		l2=${executables[$i]};
		#echo "/${e}:$l1\/$l2/"
		out=data/${e}_${l1}_${l2};
		echo "-- ${e}:${l1}/${l2} in $out";
		cat $file | awk "/${e}:$l1\/$l2/ { \$1=\" \"; \$2=\" \"; print; }" > $out;
	    done
	done
    done
fi

