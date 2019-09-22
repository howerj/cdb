/* CDB - Constant Database Library
 * Author:  Richard James Howe
 * Email:   howe.r.j.89@gmail.com
 * License: Unlicense
 * Repo:    <https://github.com/howerj/cdb> */

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

typedef struct {
	uint32_t *hashes;
	uint32_t *fps;     /* file pointers */
	uint32_t position; /* position on disk of this hash table, when known */
	size_t length;
} cdb_hash_table_t;

struct cdb {
	cdb_hash_table_t table1[256]; /* TODO: Only allocate if we are creating... */
	cdb_file_operators_t ops;
	cdb_allocator_t a;
	uint32_t position; /* used for creation only */
	int create, opened, invalid;
};

static uint32_t hash(uint8_t *s, size_t length) {
	uint32_t hash = 5381ul;
	for (size_t i = 0; i < length; i++)
		hash = ((hash << 5ul) + hash) + s[i]; /* hash * 33 + c */
	return hash;
}

static inline int cdb_free(cdb_t *cdb, void *p) {
	assert(cdb);
	if (!p)
		return 0;
	return cdb->a.free(cdb->a.arena, p);
}

static inline void *cdb_allocate(cdb_t *cdb, const size_t length) {
	assert(cdb);
	void *r = cdb->a.malloc(cdb->a.arena, length);
	return r ? memset(r, 0, length) : NULL;
}

static inline void *cdb_reallocate(cdb_t *cdb, void *pointer, const size_t length) {
	assert(cdb);
	return cdb->a.realloc(cdb->a.arena, pointer, length);
}

static inline long cdb_seek(cdb_t *cdb, long position, long whence) {
	assert(cdb);
	return cdb->ops.seek(cdb->ops.file, position, whence);
}

static inline long cdb_read(cdb_t *cdb, void *buf, size_t length) {
	assert(cdb);
	assert(buf);
	return cdb->ops.read(cdb->ops.file, buf, length);
}

static inline long cdb_write(cdb_t *cdb, void *buf, size_t length) {
	assert(cdb);
	assert(buf);
	return cdb->ops.write(cdb->ops.file, buf, length);
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

static long cdb_write_word(cdb_t *cdb, const uint32_t word) {
	assert(cdb);
	uint8_t b[sizeof word] = { 0 };
	for (size_t i = 0; i < sizeof b; i++)
		b[i] = (word >> (i * CHAR_BIT)) & 0xFFu;
	if (cdb_write(cdb, b, sizeof b) != sizeof b)
		return -1;
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

static inline long cdb_write_word_pair(cdb_t *cdb, const uint32_t w1, const uint32_t w2) {
	assert(cdb);
	if (cdb_write_word(cdb, w1) < 0)
		return -1;
	return cdb_write_word(cdb, w2);
}

int cdb_open(cdb_t **cdb, cdb_file_operators_t *ops, cdb_allocator_t *allocator, int create, const char *file) {
	assert(cdb);
	assert(ops);
	*cdb = NULL;
	cdb_t *c = allocator->malloc(allocator->arena, sizeof *c);
	if (!c)
		goto fail;
	memset(c, 0, sizeof *c);
	c->invalid = 1;
	c->ops     = *ops;
	c->a       = *allocator;
	c->create  = create;
	*cdb       = c;
	c->ops.file = c->ops.open(file, create ? CDB_RW_MODE : CDB_RO_MODE);
	if (!(c->ops.file))
		goto fail;
	c->opened = 1;

	if (cdb_seek(c, FILE_START, CDB_SEEK_START) < 0)
		goto fail;
	for (size_t i = 0; i < 256; i++) /* write empty header */
		if (cdb_write_word_pair(c, 0, 0) < 0)
			goto fail;

	c->position = FILE_START + (256ul * (2ul * sizeof(uint32_t)));
	c->invalid = 0;
	return 0;
fail:
	(void)cdb_close(c);
	return -1;
}

int cdb_close(cdb_t *cdb) { /* free cdb, close (and write to disk if in create mode) */
	if (!cdb)
		return 0;
	if (!(cdb->invalid) && cdb->create) {
		/* TODO: Write to disk... */
		if (cdb_seek(cdb, cdb->position, CDB_SEEK_START) < 0)
			return -1;
		for (size_t i = 0; i < 256; i++) {
			cdb_hash_table_t *t = &cdb->table1[i];
			const size_t length = t->length;
			if (length == 0)
				continue;
			for (size_t j = 0; j < length; j++) {
				t->position = cdb->position;
				/* TODO: Write out hash table (length should be rounded up to nearest power of 2)...*/
			}
		}

		/* - Write hash tables */
		/* - Seek to file start */
		/* - Write out */
	}
	if (cdb->ops.file)
		cdb->ops.close(cdb->ops.file);
	cdb->ops.file = NULL;
	cdb->opened = 0;
	return cdb_free(cdb, cdb);
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

/* returns: -1 = error, 0 = not found, 1 = found */
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
			if (cr > 0) { /* found! */
				*value = (cdb_file_pos_t) { .length = vlen, .position = k2.position + klen };
				return 1; 
			}
		}
	}
	return 0; /* not found */
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

static int cdb_hash_free(cdb_t *cdb, cdb_hash_table_t *t) {
	assert(cdb);
	assert(t);
	const int r1 = cdb_free(cdb, t->hashes);
	const int r2 = cdb_free(cdb, t->fps);
	t->hashes = NULL;
	t->fps    = NULL;
	/* do not free t */
	return r1 < 0 || r2 < 0 ? -1 : 0;
}

static int cdb_hash_grow(cdb_t *cdb, uint32_t hash, uint32_t position) {
	assert(cdb);
	cdb_hash_table_t *t1 = &cdb->table1[hash % 256];
	uint32_t *hashes = cdb_reallocate(cdb, t1->hashes, (t1->length + 1) * sizeof (*t1->hashes));
	uint32_t *fps    = cdb_reallocate(cdb, t1->fps,    (t1->length + 1) * sizeof (*t1->fps));
	if (!hashes || !fps) {
		(void)cdb_hash_free(cdb, t1);
		return -1;
	}
	t1->hashes = hashes;
	t1->fps    = fps;
	t1->hashes[t1->length] = hash;
	t1->fps[t1->length]    = position;
	t1->length++;
	return 0;
}

int cdb_add(cdb_t *cdb, const cdb_buffer_t *key, const cdb_buffer_t *value) {
	assert(cdb);
	assert(key);
	assert(value);
	if (cdb->create == 0)
		goto fail;
	/* TODO: (Optionally?) Look for duplicate keys (or do it after database completion) */
	const uint32_t h = hash((uint8_t*)(key->buffer), key->length);
	if (cdb_seek(cdb, cdb->position, CDB_SEEK_START) < 0)
		goto fail;
	if (cdb_hash_grow(cdb, h, cdb->position) < 0)
		goto fail;
	if (cdb_write_word_pair(cdb, key->length, value->length) < 0)
		goto fail;
	if (cdb_write(cdb, key->buffer, key->length) < 0)
		goto fail;
	if (cdb_write(cdb, value->buffer, value->length) < 0)
		goto fail;
	const uint32_t add = (2u * sizeof (uint32_t)) + key->length + value->length;
	if ((cdb->position + add) <= cdb->position) /* NB. Does not detect all overflows...*/
		return -1;
	cdb->position += add;
	return 0;
fail:
	cdb->invalid = 1;
	return -1;
}

int cdb_tests(cdb_file_operators_t *ops, cdb_allocator_t *allocator) {
	assert(ops);
	assert(allocator);
	if (!DEBUGGING)
		return 0;
	return 0;
}

