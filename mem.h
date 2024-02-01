#ifndef CDB_MEM_H
#define CDB_MEM_H

#include "cdb.h"

typedef struct {
	const cdb_callbacks_t *uniq_ptr;
	cdb_t *cdb;
	int mode;
	size_t length, pos;
	char *m;
} cdb_mem_t;

int cdb_mem(cdb_t *cdb, cdb_mem_t **mem);

extern const cdb_callbacks_t cdb_mem_options;

#endif
