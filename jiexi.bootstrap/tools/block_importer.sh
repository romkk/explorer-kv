#!/bin/bash
SROOT=$(cd $(dirname "$0"); pwd)
cd $SROOT
export LC_ALL=C

END=390620

echo > blkimport.log
for (( i=0; i<$[$END / 10000]; i=i+1 ))
do
    s=$[i*10000]
    e=$[i*10000 + 9999]
    ./blkimport -c tparser.bootstrap.conf -l blkimport.log -b $s -e $e
done

./blkimport -c tparser.bootstrap.conf -l blkimport.log -b $[$END/10000*10000] -e $END

