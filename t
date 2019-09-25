#!/bin/sh
# CDB Test Script
set -x;
set -e;
set -u;
make cdb;
./cdb -t; # BIST generates test.cdb
./cdb -d test.cdb | sort > t1.txt;
./cdb -c t2.cdb < t1.txt;
./cdb -d t2.cdb | sort > t2.txt;
cmp t1.txt t2.txt;
make clean
