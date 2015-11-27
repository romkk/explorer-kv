#! /bin/bash
SROOT=$(cd $(dirname "$0"); pwd)
cd $SROOT
export LC_ALL=C

flatc --gen-mutable -c -o ../jiexi/src explorer.fbs
#flatc --gen-mutable -c -o ../jiexi.bootstrap/src explorer.fbs

# test
flatc --gen-mutable -c -o ../jiexi/test test.fbs
