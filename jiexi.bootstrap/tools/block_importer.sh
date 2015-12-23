#!/bin/bash
SROOT=$(cd $(dirname "$0"); pwd)
cd $SROOT
export LC_ALL=C

for i in {0..37}; do
    (( s=$i*10000, e=$i*10000+9999  ))
    ./blkimport -c tparser.bootstrap.conf -l blkimport.log -b $s -e $e
done

# -b BeginHeight -e EndHeight
./blkimport -c tparser.bootstrap.conf -l blkimport.log -b 380000 -e 388499
