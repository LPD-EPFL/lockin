#!/bin/sh

app_all="ll";

dat_folder="./data";
gp_folder="./gp";
out_folder="./plots"
gp_template="./scripts/lock-pwr.gp";

[ -d "$gp_folder" ] || mkdir $gp_folder;
[ -d "$out_folder" ] || mkdir $out_folder;

rm $gp_folder/* >> /dev/null;
rm $out_folder/* >> /dev/null;

num_locks="1 512o"
num_cl="0"
read_write="0"

for cl in $num_cl
do
    echo "Accessing $cl cache lines";
    for rw in $read_write
    do
	echo "  Writing? $rw"
	for nl in $num_locks 
	do
	    echo "* Number of locks: $nl";
	    dat="$dat_folder/energy_all_locks.c$cl.w$rw.l$nl.rapl.dat";
	    gp="$gp_folder/energy_all_locks.c$cl.w$rw.l$nl.rapl.gp";
	    eps="$out_folder/energy_all_locks.c$cl.w$rw.l$nl.rapl.eps";

	    cp $gp_template $gp

	    title1="Lock Throughput (cl: $cl / w: $rw / l: $nl)";
	    title2="Lock Throughput/Power (cl: $cl / w: $rw / l: $nl)";
	    title3="Lock Energy/Operation (cl: $cl / w: $rw / l: $nl)";

	    cat << EOF >> $gp
set term postscript eps enhanced 20
set output "$eps";
set multiplot;

set title  "$title1"; 
set origin 0,0;
set size 0.9,1.4

unset key
plot \\
"$dat" using 1:(\$2) title  "TAS" ls 1 with linespoints, \\
"$dat" using 1:(\$5) title  "TTAS" ls 2 with linespoints, \\
"$dat" using 1:(\$8) title  "TICKET" ls 3 with linespoints, \\
"$dat" using 1:(\$11) title  "MCS" ls 4 with linespoints, \\
"$dat" using 1:(\$14) title  "CLH" ls 5 with linespoints, \\
"$dat" using 1:(\$17) title  "ARRAY" ls 6 with linespoints, \\
"$dat" using 1:(\$20) title  "MUTEX" ls 9 with linespoints 

set title  "$title2"; 
set size 0.9,1.4
set origin 0.9,0;
set ylabel "Throughput per Power (Ops/Joule)" offset 1.5

plot \\
"$dat" using 1:(\$4) title  "TAS" ls 1 with linespoints, \\
"$dat" using 1:(\$7) title  "TTAS" ls 2 with linespoints, \\
"$dat" using 1:(\$10) title  "TICKET" ls 3 with linespoints, \\
"$dat" using 1:(\$13) title  "MCS" ls 4 with linespoints, \\
"$dat" using 1:(\$16) title  "CLH" ls 5 with linespoints, \\
"$dat" using 1:(\$19) title  "ARRAY" ls 6 with linespoints, \\
"$dat" using 1:(\$22) title  "MUTEX" ls 9 with linespoints 

set title  "$title3"; 
set size 1.2,1.4
set origin 1.8,0;
set ylabel "Energy per Operation (uJoule/op)" offset 1.5

set key outside
plot \\
"$dat" using 1:(\$3) title  "TAS" ls 1 with linespoints, \\
"$dat" using 1:(\$6) title  "TTAS" ls 2 with linespoints, \\
"$dat" using 1:(\$9) title  "TICKET" ls 3 with linespoints, \\
"$dat" using 1:(\$12) title  "MCS" ls 4 with linespoints, \\
"$dat" using 1:(\$15) title  "CLH" ls 5 with linespoints, \\
"$dat" using 1:(\$18) title  "ARRAY" ls 6 with linespoints, \\
"$dat" using 1:(\$21) title  "MUTEX" ls 9 with linespoints 

unset multiplot
EOF
	    gnuplot $gp;
	done;

	# put several on the same pdf
	# name_prefix="energy_all_locks.c$cl.w$rw.rapl";

	# eps_mask=$name_prefix".l*.eps";
	# cd $out_folder;
	# for f in  $(ls $eps_mask)
	# do
	#     echo "     epstopdf $f";
	#     epstopdf $f;
	# done;

	# pdf_mask=$name_prefix".l*.pdf";
	# pdftk $(ls -1 $pdf_mask | sort -V) cat output $name_prefix"_sep.pdf";
	# pdfnup --a4paper --no-landscape --nup 1x4 --outfile  $name_prefix".pdf" $name_prefix"_sep.pdf";

	# cd -;

    done;
done;

# cd $out_folder;
# name_prefix="energy_all_locks";
# pdf_mask=$name_prefix".c*.w[01].pdf"
# echo "## Creating final pdf with all results: $name_prefix.pdf";
# pdftk $(ls -1 $pdf_mask | sort -V) cat output $name_prefix".pdf";
