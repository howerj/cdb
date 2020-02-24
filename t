#!/bin/sh
# CDB Test Script
#
RANDOMSRC=${RANDOMSRC:-/dev/urandom};
TESTDB=test.cdb;
EMPTYDB=empty.cdb;
FAIL=0;
PERFORMANCE=${PERFORMANCE:-test.cdb};
CDB=${CDB:-cdb};

performance () {
	set -eux;
	make test;

	time -p ./cdb -s   "${PERFORMANCE}" > /dev/null;
	time -p cdb   -s   "${PERFORMANCE}" > /dev/null;
	time -p cdbstats < "${PERFORMANCE}" > /dev/null;

	time -p ./cdb   -d "${PERFORMANCE}" > /dev/null;
	time -p cdb     -d "${PERFORMANCE}" > /dev/null;
	time -p cdbdump  < "${PERFORMANCE}" > /dev/null;

	./cdb   -d "${PERFORMANCE}" > 1.txt;
	cdb     -d "${PERFORMANCE}" > 2.txt;
	cdbdump  < "${PERFORMANCE}" > 3.txt;

	time -p ./cdb -c 1.cdb          < 1.txt;
	time -p cdb   -c 2.cdb          < 2.txt;
	time -p cdbmake  3.cdb temp.cdb < 3.txt;
}

usage () {
HELP=$(cat <<EOF
cdb test and performance suite

By default this program will run a series of tests on the various
versions of the CDB.

-h	print this help and exit successfully	
-p	do performance tests instead on default file
-P #	set CDB file for performance tests and run tests

This program will return zero and non-zero on failure.
EOF
);
	echo "${HELP}"
}

while getopts 'hpP:' opt
do
	case "${opt}" in
		h) usage; exit 0; ;;
		p) performance; exit 0; ;;
		P) PERFORMANCE="${OPTARG}"; performance; exit 0; ;;
		?) usage; exit 1; ;;
	esac
done

make ${CDB};
for SIZE in 32 16 64; do
	set -eux;

	./${CDB} -b ${SIZE} -c ${EMPTYDB} <<EOF
EOF
	./${CDB} -b ${SIZE} -t bist.cdb;
	./${CDB} -b ${SIZE} -d bist.cdb | sort > bist.txt;
	./${CDB} -b ${SIZE} -c copy.cdb -T temp.cdb < bist.txt;
	./${CDB} -b ${SIZE} -d copy.cdb | sort > copy.txt;
	diff -w bist.txt copy.txt;

	./${CDB} -b ${SIZE} -c ${TESTDB} <<EOF
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

	t "./${CDB} -b ${SIZE} -q ${TESTDB} a" b;
	t "./${CDB} -b ${SIZE} -q ${TESTDB} a 0" b;
	t "./${CDB} -b ${SIZE} -q ${TESTDB} a 1" b;
	t "./${CDB} -b ${SIZE} -q ${TESTDB} a 2" c;
	f "./${CDB} -b ${SIZE} -q ${TESTDB} a 3";
	t "./${CDB} -b ${SIZE} -q ${TESTDB} X" "";
	f "./${CDB} -b ${SIZE} -q ${TESTDB} XXX";
	t "./${CDB} -b ${SIZE} -q ${TESTDB} \"\"" X;
	t "./${CDB} -b ${SIZE} -q ${TESTDB} b" hello;
	t "./${CDB} -b ${SIZE} -q ${TESTDB} c" world;
	t "./${CDB} -b ${SIZE} -q ${TESTDB} open" seasame;

	for i in $(seq 0 9); do
		for j in $(seq 0 9); do
			for k in $(seq 0 9); do
				KEY="${i}${j}${k}"
				VAL="${i}${j}${k}"
				echo "+${#KEY},${#VAL}:${KEY}->${VAL}";
			done;
		done;
	done > seq.txt;
	echo > seq.txt

	dd if=/dev/zero of=invalid-1.cdb count=1 # Too small
	dd if=/dev/zero of=invalid-2.cdb count=4 # Invalid hash table pointers
	#dd if=${RANDOMSRC} of=invalid-3.cdb count=512

	f "./${CDB} -b ${SIZE} -s invalid-1.cdb"
	f "./${CDB} -b ${SIZE} -s invalid-2.cdb"
	#f "./${CDB} -s invalid-3.cdb"
	f "./${CDB} -b ${SIZE} -s /dev/null"

	set -x

	./${CDB} -b ${SIZE} -c seq.cdb < seq.txt;
	./${CDB} -b ${SIZE} -d seq.cdb | sort > qes.txt;

	diff -w seq.txt qes.txt;

	./${CDB} -b ${SIZE} -s ${EMPTYDB}
	./${CDB} -b ${SIZE} -s seq.cdb;
	./${CDB} -b ${SIZE} -s ${TESTDB}
	./${CDB} -b ${SIZE} -s bist.cdb;

	dd if=/dev/zero of=offset.bin count=4 bs=512
	cat offset.bin test.cdb > offset.cdb
	./${CDB} -o 2048 -b ${SIZE} -V offset.cdb;

	set +x;
done;

make clean
exit ${FAIL};
