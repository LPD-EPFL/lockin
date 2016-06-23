#!/bin/bash

. scripts/config;

LOCKS="MUTEX TAS TTAS TICKETDVFS"

UNAMEN=$(uname -n);

if [ $UNAMEN = "lpdpc4" ];
then
    printf "";
fi;

if [ $UNAMEN = "lpdpc34" ];
then
    printf "";
fi;


usage()
{
    echo "$0 [-v] [-s suffix]";
    echo "    -v             verbose";
    echo "    -s suffix      suffix the executable with suffix";
}


USUFFIX="";
VERBOSE=0;
 while getopts "hs:v" OPTION
 do
      case $OPTION in
          h)
	      usage;
              exit 1
              ;;
          s)
              USUFFIX="_$OPTARG"
	      echo "Using suffix: $USUFFIX"
              ;;
          v)
              VERBOSE=1
              ;;
          ?)
	      usage;
              exit;
              ;;
      esac
 done

all="stress_queued_in";
pause=( "empty" "nop" "pause" "mfence" "3xnop" "none" )

for lock in $LOCKS
do
    echo "Building: $lock";

    for (( i=0; i < ${#pause[@]}; i++))
    do
	echo "   with: ${pause[$i]}";
	if [ $VERBOSE -eq 1 ]; 
	then
	    LOCK_IN=$lock PAUSE_IN=$i $MAKE $all;
	else
	    LOCK_IN=$lock PAUSE_IN=$i $MAKE $all  > /dev/null;
	fi

	for e in $all;
	do
	    mv ${e} ${e}_${lock}_${pause[$i]};
	done;
    done;
done;
