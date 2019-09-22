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

all: ${TARGET}

cdb.o: cdb.c cdb.h

main.o: main.c cdb.h

lib${TARGET}.a: ${TARGET}.o ${TARGET}.h
	${AR} ${ARFLAGS} $@ $<
	${RANLIB} $@


${TARGET}: main.o lib${TARGET}.a

test: ${TARGET}
	./${TARGET} -t

clean:
	git clean -dfx
