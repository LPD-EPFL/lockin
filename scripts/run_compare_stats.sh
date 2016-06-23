#!/bin/bash

infile=$1;

if [[ ! -f "$infile" ]];
then
    echo "** File $infile not found!";
    exit;
fi;


awk '/##/ { 
  i++;
  thr1+=$3; thr2+=$5; thr3+=$7;
  eop1+=$4; eop2+=$6; eop3+=$8;

  if ( $3 > $5 ) { a_thr_m_a++; };
  if ( $3 > $7 ) { a_thr_m_e++; };
  if ( $5 > $7 ) { a_thr_a_e++; };
  if ( $4 < $6 ) { a_eop_m_a++; };
  if ( $4 < $8 ) { a_eop_m_e++; };
  if ( $6 < $8 ) { a_eop_a_e++; };

  if ( $9 == "MUTEX" ) { 
    thr[0]++ 
    thr_m_a += $3/$5;
    thr_m_e += $3/$7;
  };
  if ( $9 == "MUTEXA" ) { 
    thr[1]++ 
    thr_a_m += $5/$3;
    thr_a_e += $5/$7;
  };
  if ( $9 == "MUTEXEE" ) { 
    thr[2]++ 
    thr_e_m += $7/$3;
    thr_e_a += $7/$5;
  };
  if ( $10 == "MUTEX" ) { 
    eop[0]++ 
    eop_m_a += $3/$5;
    eop_m_e += $3/$7;
  };
  if ( $10 == "MUTEXA" ) { 
    eop[1]++ 
    eop_a_m += $5/$3;
    eop_a_e += $5/$7;
  };
  if ( $10 == "MUTEXEE" ) { 
    eop[2]++ 
    eop_e_m += $7/$3;
    eop_e_a += $7/$5;
  };
}; 
END { 
  printf "# samples       %d\n", i;
  printf "lock            %-6s %-6s %-6s \n", "MUTEX", "MUTEXA", "MUTEXEE";
  printf "throughput      %-6d %-6d %-6d \n", thr1/i, thr2/i, thr3/i;
  printf "#best thr       %-6d %-6d %-6d \n", thr[0], thr[1], thr[2];
  printf "mut thr by      %-6.2f %-6.2f %-6.2f \n", 1, thr_m_a/thr[0], thr_m_e/thr[0];
  printf "mua thr by      %-6.2f %-6.2f %-6.2f \n", thr_a_m/thr[1], 1, thr_a_e/thr[1];
  printf "mue thr by      %-6.2f %-6.2f %-6.2f \n", thr_e_m/thr[2], thr_e_a/thr[2], 1;
  it = i * 1000;
  printf "tpp             %-6.2f %-6.2f %-6.2f \n", it/eop1, it/eop2, it/eop3;
  printf "eop             %-6.2f %-6.2f %-6.2f \n", eop1/i, eop2/i, eop3/i;
  printf "#best eop       %-6.2f %-6.2f %-6.2f \n", eop[0], eop[1], eop[2];
  printf "mut eop by      %-6.2f %-6.2f %-6.2f \n", 1, eop_m_a/eop[0], eop_m_e/eop[0];
  printf "mua eop by      %-6.2f %-6.2f %-6.2f \n", eop_a_m/eop[1], 1, eop_a_e/eop[1];
  printf "mue eop by      %-6.2f %-6.2f %-6.2f \n", eop_e_m/eop[2], eop_e_a/eop[2], 1;
  print "-- MUTEX vs. MUTEXA";
  print "MUTEX bett thr   " a_thr_m_a, i - a_thr_m_a;
  print "MUTEX bett eop   " a_eop_m_a, i - a_eop_m_a;
  print "-- MUTEX vs. MUTEXEE";
  print "MUTEX bett thr   " a_thr_m_e, i - a_thr_m_e;
  print "MUTEX bett eop   " a_eop_m_e, i - a_eop_m_e;
  print "-- MUTEXA vs. MUTEXEE";
  print "MUTEX bett thr   " a_thr_a_e, i - a_thr_a_e;
  print "MUTEX bett eop   " a_eop_a_e, i - a_eop_a_e;
}' $infile;
