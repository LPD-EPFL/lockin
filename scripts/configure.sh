#!/bin/bash

ef=external;
lib=raplread;
llib=https://github.com/LPD-EPFL/raplread

cd ${ef};
if [ ! -d ${lib} ];
then
    echo "# $lib not found! Cloning!";
    git clone ${llib};
    echo "# !! Warning: You might need to configure ${lib} !!";
    echo "# !! Follow the instructions @ ${llib} and rerun !!";
    echo "# !! $0 script !!";
fi;

cd $lib;

make >> /dev/null && cp lib${lib}.a ../../;
cd ../;


