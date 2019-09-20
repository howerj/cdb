CFLAGS=-Wall -Wextra -std=c99 -O2 -pedantic
TARGET=cdb

all: ${TARGET}

cdb.o: cdb.c cdb.h

main.o: main.c cdb.h

${TARGET}: main.o ${TARGET}.o

clean:
	git clean -dfx
