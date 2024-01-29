#ifndef CDB_MEM_H
#define CDB_MEM_H

#include "cdb.h"

typedef struct {
	cdb_t *cdb;
	const cdb_options_t *uniq_ptr;
	int mode;
	size_t length, pos;
	char *m;
} cdb_mem_t;

cdb_mem_t *cdb_mem(cdb_t *cdb);

extern const cdb_options_t cdb_mem_options;

#endif
