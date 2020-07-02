# CDB makefile - default target should build everything
#
VERSION =0x040301ul
CFLAGS  =-Wall -Wextra -fPIC -std=c99 -O2 -pedantic -fwrapv -DCDB_VERSION="${VERSION}" ${DEFINES} ${EXTRA} 
TARGET  =cdb
AR      =ar
ARFLAGS =rcs
RANLIB  =ranlib
DESTDIR =install

ifeq ($(OS),Windows_NT)
DLL=dll
else # Assume Unixen
DLL=so
CFLAGS+=-D_FILE_OFFSET_BITS=64 
endif

.PHONY: all test clean dist install

all: ${TARGET}

cdb.o: cdb.c cdb.h makefile

main.o: main.c cdb.h makefile

lib${TARGET}.a: ${TARGET}.o ${TARGET}.h
	${AR} ${ARFLAGS} $@ $<
	${RANLIB} $@

lib${TARGET}.${DLL}: ${TARGET}.o ${TARGET}.h
	${CC} ${CFLAGS} -shared ${TARGET}.o -o $@

${TARGET}: main.o lib${TARGET}.a
	${CC} $^ -o $@
	-strip ${TARGET}

test: ${TARGET}
	./${TARGET} -t test.cdb

${TARGET}.1: readme.md
	-pandoc -s -f markdown -t man $< -o $@

.git:
	git clone https://github.com/howerj/cdb cdb-repo
	mv cdb-repo/.git .
	rm -rf cdb-repo

install: ${TARGET} lib${TARGET}.a lib${TARGET}.${DLL} ${TARGET}.1 .git
	install -p -D ${TARGET} ${DESTDIR}/bin/${TARGET}
	install -p -m 644 -D lib${TARGET}.a ${DESTDIR}/lib/lib${TARGET}.a
	install -p -D lib${TARGET}.${DLL} ${DESTDIR}/lib/lib${TARGET}.${DLL}
	install -p -m 644 -D ${TARGET}.h ${DESTDIR}/include/${TARGET}.h
	-install -p -m 644 -D ${TARGET}.1 ${DESTDIR}/man/${TARGET}.1
	mkdir -p ${DESTDIR}/src
	cp -a .git ${DESTDIR}/src
	cd ${DESTDIR}/src && git reset --hard HEAD

dist: install
	tar zcf ${TARGET}-${VERSION}.tgz ${DESTDIR}

clean: .git
	git clean -dffx


