#!/bin/sh
# CDB Test Script
#
RANDOMSRC=${RANDOMSRC:-/dev/urandom};
TESTDB=test.cdb;
EMPTYDB=empty.cdb;
FAIL=0;
PERFORMANCE=${PERFORMANCE:-test.cdb};
#CDB=${CDB:-cdb};

performance () {
	set -eux;
	make test;

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

for CDB in cdb cdb16 cdb32 cdb64; do
	set -eux;

	make ${CDB};

	./${CDB} -c ${EMPTYDB} <<EOF
EOF
	./${CDB} -t bist.cdb;
	./${CDB} -d bist.cdb | sort > bist.txt;
	./${CDB} -c copy.cdb -T temp.cdb < bist.txt;
	./${CDB} -d copy.cdb | sort > copy.txt;
	diff -w bist.txt copy.txt;

	./${CDB} -c ${TESTDB} <<EOF
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

	t "./${CDB} -q ${TESTDB} a" b;
	t "./${CDB} -q ${TESTDB} a 0" b;
	t "./${CDB} -q ${TESTDB} a 1" b;
	t "./${CDB} -q ${TESTDB} a 2" c;
	f "./${CDB} -q ${TESTDB} a 3";
	t "./${CDB} -q ${TESTDB} X" "";
	f "./${CDB} -q ${TESTDB} XXX";
	t "./${CDB} -q ${TESTDB} \"\"" X;
	t "./${CDB} -q ${TESTDB} b" hello;
	t "./${CDB} -q ${TESTDB} c" world;
	t "./${CDB} -q ${TESTDB} open" seasame;

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

	f "./${CDB} -s invalid-1.cdb"
	f "./${CDB} -s invalid-2.cdb"
	#f "./${CDB} -s invalid-3.cdb"
	f "./${CDB} -s /dev/null"

	set -x

	./${CDB} -c seq.cdb < seq.txt;
	./${CDB} -d seq.cdb | sort > qes.txt;

	diff -w seq.txt qes.txt;

	./${CDB} -s ${EMPTYDB}
	./${CDB} -s seq.cdb;
	./${CDB} -s ${TESTDB}
	./${CDB} -s bist.cdb;

	set +x;

	# # Extra stuff, not used yet as it needs speeding up. A PRNG could
	# # be made in pure sh to do so, otherwise we are shelling out too much.
	#
	# r999 () {
	# 	echo $(cat ${RANDOMSRC} | tr -dc '0-9' | fold -w 256 | head -n 1 | sed -e 's/^0*//' | head --bytes 3);
	# }
	#
	# rlen() {
	# 	echo $(cat ${RANDOMSRC} | tr -dc 'a-zA-Z0-9' | fold -w "${1}" | head -n 1)
	# }
	#
	# FILE=rnd
	# rm -fv "${FILE}.txt ${FILE}.cdb";
	# for i in $(seq 1 1000); do
	# 	KLEN=$(r999);
	# 	VLEN=$(r999);
	# 	KEY=$(rlen ${KLEN})
	# 	VALUE=$(rlen ${VLEN})
	# 	echo "+${KLEN},${VLEN}:${KEY}->${VALUE}" >> ${FILE}.txt
	# done;
	#
	# ./${CDB} -c ${FILE}.cdb < ${FILE}.txt

done;

make clean
exit ${FAIL};
