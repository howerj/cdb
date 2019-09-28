# CDB makefile - default target should build everything
#
# <https://news.ycombinator.com/item?id=15400396>
# EXTRA = -Wduplicated-cond -Wlogical-op \
#	-Wnull-dereference -Wjump-misses-init \
#	-Wshadow 

CFLAGS=-Wall -Wextra -std=c99 -O2 -pedantic -g -fwrapv ${DEFINES} ${EXTRA}
TARGET=cdb
AR      = ar
ARFLAGS = rcs
RANLIB  = ranlib
DESTDIR = install

.PHONY: all test clean dist install

all: ${TARGET}

cdb.o: cdb.c cdb.h

main.o: main.c cdb.h

lib${TARGET}.a: ${TARGET}.o ${TARGET}.h
	${AR} ${ARFLAGS} $@ $<
	${RANLIB} $@

${TARGET}: main.o lib${TARGET}.a

test: ${TARGET}
	./${TARGET} -t t1.cdb

cdb.1: readme.md
	pandoc -s -f markdown -t man $< -o $@

install: ${TARGET} lib${TARGET}.a cdb.1
	install -p -D ${TARGET} ${DESTDIR}/bin/${TARGET}
	install -p -m 644 -D lib${TARGET}.a ${DESTDIR}/lib/lib${TARGET}.a
	install -p -m 644 -D cdb.1 ${DESTDIR}/man/cdb.1
	mkdir -p ${DESTDIR}/src
	install -p -m 644 -D cdb.c cdb.h main.c LICENSE readme.md makefile -t ${DESTDIR}/src
	install -p -D t ${DESTDIR}/src/t

dist: install
	tar zcf ${TARGET}.tgz ${DESTDIR}

clean:
	git clean -dfx
