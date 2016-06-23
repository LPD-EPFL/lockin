set xlabel "Number of threads"
# set y2label "Scalability"

#nomirror - no tics on the right and top
#scale - the size of the tics
set xtics 1 nomirror scale 2
set ytics auto nomirror scale 2
# set y2tics auto nomirror scale 2

#remove top and right borders
set border 3 back
#add grid
set style line 12 lc rgb '#808080' lt 2 lw 1
set grid back ls 12

set xrange [1:]
set yrange [0:]

#the size of the graph
set size 2,1.0

#some nice line colors
#lc: line color; lt: line type (1 - conitnuous); pt: point type
#ps: point size; lw: line width
set style line 1 lc rgb '#0060ad' lt 1 pt 2 ps 1.5 lw 4 
set style line 2 lc rgb '#dd181f' lt 1 pt 5 ps 2 lw 4
set style line 3 lc rgb '#8b1a0e' pt 1 ps 1.5 lt 1 lw 4
set style line 4 lc rgb '#5e9c36' pt 6 ps 2 lt 2 lw 4
set style line 5 lc rgb '#663399' lt 1 pt 3 ps 1.5 lw 4 
set style line 8 lc rgb '#299fff' lt 1 pt 8 ps 2 lw 4
set style line 9 lc rgb '#ff299f' lt 1 pt 9 ps 2 lw 4
set style line 6 lc rgb '#cc6600' lt 4 pt 4 ps 2 lw 4
set style line 7 lc rgb '#cccc00' lt 1 pt 7 ps 2 lw 4

#move the legend to a custom position (can also be moved to absolute coordinates)
set key outside

set terminal postscript color  "Helvetica" 24 eps enhanced

#set term tikz standalone color solid size 5in,3in
#set output "test.tex"

#for more details on latex, see http://www.gnuplotting.org/introduction/output-terminals/
#set term epslatex #size 3.5, 2.62 #color colortext
#size can also be in cm: set terminal epslatex size 8.89cm,6.65cm color colortext
#set output "test.eps"

set term postscript eps enhanced 20
set output "./plots/queue_cpi_all_locks_rapl.eps";
set multiplot;

unset title

set ylabel "Power consumption (Watts)" offset 1
#set title  "Power consumption (Watts)"; 
set origin 0,0;
set size 0.9,1.0

unset key
plot \
"./data/cpi_all_locks_rapl.dat" using 1:($4) title  "TAS" ls 1 with linespoints, \
"./data/cpi_all_locks_rapl.dat" using 1:($7) title  "TTAS" ls 2 with linespoints, \
"./data/cpi_all_locks_rapl.dat" using 1:($10) title  "TICKET" ls 3 with linespoints, \
"./data/cpi_all_locks_rapl.dat" using 1:($13) title  "MCS" ls 4 with linespoints, \
"./data/cpi_all_locks_rapl.dat" using 1:($16) title  "CLH" ls 5 with linespoints, \
"./data/cpi_all_locks_rapl.dat" using 1:($19) title  "ARRAY" ls 6 with linespoints, \
"./data/cpi_all_locks_rapl.dat" using 1:($22) title  "MUTEX" ls 9 with linespoints 

set key outside
set size 1.1,1.0
set origin 0.9,0;
set ylabel "CPI" offset 1
#set title  "CPI"; 
plot \
"./data/cpi_all_locks_rapl.dat" using 1:($2) title  "TAS" ls 1 with linespoints, \
"./data/cpi_all_locks_rapl.dat" using 1:($5) title  "TTAS" ls 2 with linespoints, \
"./data/cpi_all_locks_rapl.dat" using 1:($8) title  "TICKET" ls 3 with linespoints, \
"./data/cpi_all_locks_rapl.dat" using 1:($11) title  "MCS" ls 4 with linespoints, \
"./data/cpi_all_locks_rapl.dat" using 1:($14) title  "CLH" ls 5 with linespoints, \
"./data/cpi_all_locks_rapl.dat" using 1:($17) title  "ARRAY" ls 6 with linespoints, \
"./data/cpi_all_locks_rapl.dat" using 1:($20) title  "MUTEX" ls 9 with linespoints 


