#include "cdb.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#ifdef NDEBUG
#define DEBUGGING (0)
#else
#define DEBUGGING (1)
#endif

#define implies(P, Q) assert(!(P) || (Q))
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#define FILE_START (0)

struct cdb {
	cdb_file_operators_t ops;
	cdb_allocator_t a;
};

static uint32_t hash(uint8_t *s, size_t length) {
	uint32_t hash = 5381ul;
	for (size_t i = 0; i < length; i++)
		hash = ((hash << 5ul) + hash) + s[i]; /* hash * 33 + c */
	return hash;
}

int cdb_open(cdb_t **cdb, cdb_file_operators_t *ops, cdb_allocator_t *allocator, int create) {
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

static inline long cdb_seek(cdb_t *cdb, long position, long whence) {
	assert(cdb);
	return cdb->ops.seek(cdb->ops.file, position, whence);
}

static inline long cdb_read(cdb_t *cdb, void *buf, size_t length) {
	assert(cdb);
	return cdb->ops.read(cdb->ops.file, buf, length);
}

static long cdb_read_word(cdb_t *cdb, uint32_t *word) {
	assert(cdb);
	assert(word);
	uint8_t b[sizeof *word] = { 0 };
	*word = 0;
	const long r = cdb_read(cdb, b, sizeof b);
	if (r != sizeof b)
		return -1;
	uint32_t w = 0;
	for (size_t i = 0; i < sizeof w; i++)
		w |= b[i] << (i * CHAR_BIT);
	*word = w;
	return 0;
}

static inline long cdb_read_word_pair(cdb_t *cdb, uint32_t *w1, uint32_t *w2) {
	assert(cdb);
	assert(w1);
	assert(w2);
	if (cdb_read_word(cdb, w1) < 0)
		return -1;
	return cdb_read_word(cdb, w2);
}

/* returns: -1 = error, 0 = not equal, 1 = equal */
static int cdb_compare(cdb_t *cdb, const cdb_buffer_t *k1, const cdb_file_pos_t *k2) {
	assert(cdb);
	assert(k1);
	assert(k2);
	uint8_t kbuf[256] = { 0 };
	if (k1->length != k2->length)
		return 0; /* not equal */
	const size_t length = k1->length;
	if (cdb_seek(cdb, k2->position, CDB_SEEK_START) < 0)
		return -1;
	for (size_t i = 0; i < length; i += sizeof kbuf) {
		const size_t rl = MIN(sizeof kbuf, length - i);
		if (cdb_read(cdb, kbuf, sizeof kbuf) < 0)
			return -1;
		if (memcmp(k1->buffer, kbuf, rl))
			return 0;
	}
	return 1; /* equal */
}

int cdb_get(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value) {
	assert(cdb);
	assert(key);
	assert(value);
	*value = (cdb_file_pos_t) { 0, 0 };
	/* locate key in first table */
	const uint32_t h = hash((uint8_t *)key->buffer, key->length);
	if (cdb_seek(cdb, FILE_START + (h % 256) * (2u * sizeof(uint32_t)), CDB_SEEK_START) < 0)
		return -1;
	uint32_t pos = 0, num = 0;
	if (cdb_read_word_pair(cdb, &pos, &num) < 0)
		return -1;
	if (num == 0) /* no keys in this bucket -> key not found */
		return 0;
	const uint32_t start = (h >> 8) % num;
	for (size_t i = 0; i < num; i++) {
		if (cdb_seek(cdb, pos + ((start + i) % num) * (2u * sizeof(uint32_t)), CDB_SEEK_START) < 0)
			return -1;
		uint32_t h1 = 0, p1 = 0;
		if (cdb_read_word_pair(cdb, &h1, &p1) < 0)
			return -1;
		if (p1 == 0) /* end of list */
			return 0;
		if (h1 == h) { /* possible match */
			if (cdb_seek(cdb, p1, CDB_SEEK_START) < 0)
				return -1;
			uint32_t klen = 0, vlen = 0;
			if (cdb_read_word_pair(cdb, &klen, &vlen) < 0)
				return -1;
			const cdb_file_pos_t k2 = { .length = klen, .position = p1 + (2u * sizeof(uint32_t)) };
			const int cr = cdb_compare(cdb, key, &k2);
			if (cr < 0)
				return -1;
			if (cr > 0)
				return 1; /* found! TODO: Return value */
		}
	}
	return 0;
}

int cdb_foreach(cdb_t *cdb, cdb_callback cb, void *param) {
	assert(cdb);
	assert(cb);
	for (int i = 0; i < 256; i++) {
		if (cdb_seek(cdb, FILE_START + (i * (2u * sizeof(uint32_t))), CDB_SEEK_START) < 0)
				return -1;
		uint32_t pos = 0, num = 0;
		if (cdb_read_word_pair(cdb, &pos, &num) < 0)
			return -1;
		for (size_t j = 0; j < num; j++) {
			if (cdb_seek(cdb, pos + (j * (2u * sizeof(uint32_t))), CDB_SEEK_START) < 0)
				return -1;
			uint32_t h1 = 0, p1 = 0;
			if (cdb_read_word_pair(cdb, &h1, &p1) < 0)
				return -1;
			if (p1 == 0)
				continue;
			if (cdb_seek(cdb, p1, CDB_SEEK_START) < 0)
				return -1;
			uint32_t klen = 0, vlen = 0;
			if (cdb_read_word_pair(cdb, &klen, &vlen) < 0)
				return -1;
			const cdb_file_pos_t key   = { .length = klen, .position = (2u * sizeof(uint32_t)), };
		       	const cdb_file_pos_t value = { .length = vlen, .position = (2u * sizeof(uint32_t)) + klen, };
			if (cb(cdb, &key, &value, param) < 0)
				return -1;
		}
	}
	return 0;
}

int cdb_add(cdb_t *cdb, const cdb_buffer_t *key, const cdb_buffer_t *value) {
	assert(cdb);
	assert(key);
	assert(value);
	return 0;
}

int cdb_tests(cdb_file_operators_t *ops, cdb_allocator_t *allocator) {
	assert(ops);
	assert(allocator);
	if (!DEBUGGING)
		return 0;
	return 0;
}

