#include "cdb.h"
#include <assert.h>
#include <stdint.h>

#define implies(P, Q) assert(!(P) || (Q))

struct cdb {
	cdb_file_operators_t ops;
	cdb_allocator_t allocator;
};

static uint32_t hash(unsigned char *s, size_t length) {
	uint32_t hash = 5381ul;
	for (size_t i = 0; i < length; i++)
		hash = ((hash << 5ul) + hash) + s[i]; /* hash * 33 + c */
	return hash;
}

int cdb_open(cdb_t **cdb, cdb_file_operators_t *ops, cdb_allocator_t *allocator) {
	assert(cdb);
	assert(ops);
	if (allocator) {
	}
	return 0;
}

int cdb_close(cdb_t *cdb) {
	if (!cdb)
		return 0;
	return 0;
}

int cdb_get(cdb_t *cdb, const cdb_buffer_t *key, cdb_buffer_t **value) {
	assert(cdb);
	assert(key);
	return 0;
}

int cdb_foreach(cdb_t *cdb, cdb_callback cb, void *param) {
	assert(cdb);
	assert(cb);
	return 0;
}

static int cdb_add(cdb_t *cdb, const cdb_buffer_t *key, cdb_buffer_t *value) {
	assert(cdb);
	assert(key);
	assert(value);
	return 0;
}

int cdb_make(cdb_t *cdb, cdb_buffer_t *keys, cdb_buffer_t *values, size_t length) {
	assert(cdb);
	implies(length, keys);
	implies(length, values);
	for (size_t i = 0; i < length; i++)
		if (cdb_add(cdb, &keys[i], &values[i]) < 0)
			return -1;
	return 0;
}

