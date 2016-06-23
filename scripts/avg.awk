#!/usr/bin/awk -f

BEGIN{}

// { reps++; c1+=$1; c2+=$2; c3+=$3; c4+=$4; c5+=$5; c6+=$6; }

END{
    print c1/reps,c2/reps,c3/reps,c4/reps,c5/reps,c6/reps;
}
