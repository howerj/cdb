#!/bin/sh
# CDB Test Script
# - TODO: User Interface Tests, CLI options/help, ...
set -x;
set -e;
set -u;
make cdb;
./cdb -t 1.cdb; 
./cdb -d 1.cdb | sort > 1.txt;
./cdb -c 2.cdb < 1.txt;
./cdb -d 2.cdb | sort > 2.txt;

./cdb -c 3.cdb <<EOF
+0,1:->X
+1,0:X->
+1,1:a->b
+1,1:a->b
+1,5:b->hello
+1,5:c->world
+4,7:open->seasame
EOF

./cdb -q 3.cdb a
./cdb -q 3.cdb a 0
./cdb -q 3.cdb a 1
./cdb -q 3.cdb a 2 && exit 1
./cdb -q 3.cdb X
./cdb -q 3.cdb ""
./cdb -q 3.cdb XXX && exit 1
./cdb -q 3.cdb b
./cdb -q 3.cdb c
./cdb -q 3.cdb open

cmp 1.txt 2.txt;
# make clean
