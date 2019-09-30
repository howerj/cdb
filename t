#!/bin/sh
# CDB Test Script
# - TODO: User Interface Tests, CLI options/help, Invalid file, ...
RANDOMSRC=${RANDOMSRC:-/dev/urandom}
TESTDB=test.cdb
EMPTYDB=empty.cdb
set -x;
set -e;
set -u;

make cdb;

./cdb -c ${EMPTYDB} <<EOF
EOF
./cdb -t bist.cdb; 
./cdb -d bist.cdb | sort > bist.txt;
./cdb -c copy.cdb < bist.txt;
./cdb -d copy.cdb | sort > copy.txt;
cmp bist.txt copy.txt;

./cdb -c ${TESTDB} <<EOF
+0,1:->X
+1,0:X->
+1,1:a->b
+1,1:a->b
+1,1:a->c
+1,5:b->hello
+1,5:c->world
+4,7:open->seasame
EOF
set +x;

FAIL=0

t() {
	R=$(eval "${1}");
	if [ "${R}" != "${2}" ]; then
		echo "FAIL: '${1}' != '${2}'";
		FAIL=1;
	else
		echo "ok:  '${1}' = '${2}'";
	fi;
}

f() {
	C=1
	R=$(eval "${1}") || C=$?;
	if [ "${R}" = "0" ]; then
		echo "FAIL: '${1} == ${2}' expected a failure";
		FAIL=1;
	else
		echo "ok:  '${1}' reports failure as expected: ${C}/${R}";
	fi;
}

t "./cdb -q ${TESTDB} a" b;
t "./cdb -q ${TESTDB} a 0" b;
t "./cdb -q ${TESTDB} a 1" b;
t "./cdb -q ${TESTDB} a 2" c;
f "./cdb -q ${TESTDB} a 3";
t "./cdb -q ${TESTDB} X" "";
f "./cdb -q ${TESTDB} XXX";
t "./cdb -q ${TESTDB} \"\"" X;
t "./cdb -q ${TESTDB} b" hello;
t "./cdb -q ${TESTDB} c" world;
t "./cdb -q ${TESTDB} open" seasame;

for i in $(seq 0 9); do
	for j in $(seq 0 9); do
		for k in $(seq 0 9); do
			KEY="${i}${j}${k}"
			VAL="${i}${j}${k}"
			echo "+${#KEY},${#VAL}:${KEY}->${VAL}";
		done;
	done;
done > seq.txt;

set -x

./cdb -c seq.cdb < seq.txt;
./cdb -d seq.cdb | sort > qes.txt;

cmp seq.txt qes.txt;

./cdb -s ${EMPTYDB}
./cdb -s seq.cdb;
./cdb -s ${TESTDB}
./cdb -s bist.cdb;

set +x;

# make clean
exit ${FAIL};

# Extra stuff, not used yet as it needs speeding up. A PRNG could
# be made in pure sh to do so, otherwise we are shelling out too much.

r999 () {
	echo $(cat ${RANDOMSRC} | tr -dc '0-9' | fold -w 256 | head -n 1 | sed -e 's/^0*//' | head --bytes 3);
}

rlen() {
	echo $(cat ${RANDOMSRC} | tr -dc 'a-zA-Z0-9' | fold -w "${1}" | head -n 1)
}

FILE=rnd
rm -fv "${FILE}.txt ${FILE}.cdb";
for i in $(seq 1 1000); do
	KLEN=$(r999);
	VLEN=$(r999);
	KEY=$(rlen ${KLEN})
	VALUE=$(rlen ${VLEN})
	echo "+${KLEN},${VLEN}:${KEY}->${VALUE}" >> ${FILE}.txt
done;

./cdb -c ${FILE}.cdb < ${FILE}.txt


