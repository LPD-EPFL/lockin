#!/bin/bash

rm -f glk.o glk.so mcs.o libmcs.so

gcc -c -fPIC ./src/mcs_in.c -o mcs.o -I./include/
gcc -shared -Wl,-soname,libmcs_dynamic.so -o ./libmcs_dynamic.so mcs.o

gcc -D_GNU_SOURCE -c -fPIC ./src/glk.c -o glk.o -I./include/
gcc -D_GNU_SOURCE -shared -Wl,-soname,libglk_dynamic.so -o ./libglk_dynamic.so glk.o

gcc -c -fPIC ./src/gls.c -o gls.o -I./include/
gcc -shared -Wl,-soname,libgls_dynamic.so -o ./libgls_dynamic.so gls.o

