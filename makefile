# CDB makefile - default target should build everything
#
# <https://news.ycombinator.com/item?id=15400396>
# EXTRA = -Wduplicated-cond -Wlogical-op \
#	-Wnull-dereference -Wjump-misses-init \
#	-Wshadow 

VERSION=0x010202ul
CFLAGS=-Wall -Wextra -fPIC -std=c99 -O2 -pedantic -g -fwrapv ${DEFINES} ${EXTRA} -DCDB_VERSION="${VERSION}"
TARGET=cdb
AR      = ar
ARFLAGS = rcs
RANLIB  = ranlib
DESTDIR = install

ifeq ($(OS),Windows_NT)
DLL=dll
else # Assume Unixen
DLL=so
endif

.PHONY: all test clean dist install

all: ${TARGET}

cdb.o: cdb.c cdb.h

main.o: main.c cdb.h

lib${TARGET}.a: ${TARGET}.o ${TARGET}.h
	${AR} ${ARFLAGS} $@ $<
	${RANLIB} $@

lib${TARGET}.${DLL}: ${TARGET}.o ${TARGET}.h
	${CC} ${CFLAGS} -shared ${TARGET}.o -o $@

${TARGET}: main.o lib${TARGET}.a

test: ${TARGET}
	./${TARGET} -t t1.cdb

${TARGET}.1: readme.md
	-pandoc -s -f markdown -t man $< -o $@

install: ${TARGET} lib${TARGET}.a lib${TARGET}.${DLL} ${TARGET}.1
	install -p -D ${TARGET} ${DESTDIR}/bin/${TARGET}
	install -p -m 644 -D lib${TARGET}.a ${DESTDIR}/lib/lib${TARGET}.a
	install -p -D lib${TARGET}.${DLL} ${DESTDIR}/lib/lib${TARGET}.${DLL}
	install -p -m 644 -D ${TARGET}.h ${DESTDIR}/include/${TARGET}.h
	-install -p -m 644 -D ${TARGET}.1 ${DESTDIR}/man/${TARGET}.1
	mkdir -p ${DESTDIR}/src
	install -p -m 644 -D ${TARGET}.c ${TARGET}.h main.c LICENSE readme.md makefile -t ${DESTDIR}/src
	install -p -D t ${DESTDIR}/src/t

dist: install
	tar zcf ${TARGET}-${VERSION}.tgz ${DESTDIR}

clean:
	git clean -dfx

cdb64: DEFINES=-DCDB_SIZE=64
cdb64: main.c ${TARGET}.c ${TARGET}.h
	${CC} ${CFLAGS} main.c -c -o main.o
	${CC} ${CFLAGS} ${TARGET}.c -c -o ${TARGET}.o
	${CC} ${CFLAGS} main.o ${TARGET}.o -o $@

cdb32: DEFINES=-DCDB_SIZE=32
cdb32: main.c ${TARGET}.c ${TARGET}.h
	${CC} ${CFLAGS} main.c -c -o main.o
	${CC} ${CFLAGS} ${TARGET}.c -c -o ${TARGET}.o
	${CC} ${CFLAGS} main.o ${TARGET}.o -o $@

cdb16: DEFINES=-DCDB_SIZE=16
cdb16: main.c ${TARGET}.c ${TARGET}.h
	${CC} ${CFLAGS} main.c -c -o main.o
	${CC} ${CFLAGS} ${TARGET}.c -c -o ${TARGET}.o
	${CC} ${CFLAGS} main.o ${TARGET}.o -o $@

