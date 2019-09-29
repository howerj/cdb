/* Program: Constant Database Library
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

#ifndef MEMORY_INDEX /* use in memory hash table if '1' for first table */
#define MEMORY_INDEX (0)
#endif

#define BUILD_BUG_ON(condition)   ((void)sizeof(char[1 - 2*!!(condition)]))
#define implies(P, Q)             assert(!(P) || (Q))
#define MIN(X, Y)                 ((X) < (Y) ? (X) : (Y))
#define FILE_START                (0ul)
#define BUCKETS                   (256)
#define READ_BUFFER_LENGTH        (256)

enum {
	CDB_OK_E             =   0, /* no error */
	CDB_NOT_FOUND_E      =   0, /* key: not-found */
	CDB_FOUND_E          =   1, /* key: found */
	CDB_ERROR_E          =  -1, /* generic error */
	CDB_ERROR_HASH_E     =  -2, /* unexpected hash value given bucket */
	CDB_ERROR_BOUND_E    =  -3, /* pointers out of bounds */
	CDB_ERROR_OVERFLOW_E =  -4, /* some calculation overflowed and should not have */
	CDB_ERROR_OPEN_E     =  -5, /* open failed */
	CDB_ERROR_SEEK_E     =  -6, /* seek failed */
	CDB_ERROR_WRITE_E    =  -7, /* write failed to write any/enough bytes */
	CDB_ERROR_READ_E     =  -8, /* read failed to read any/enough bytes */
	CDB_ERROR_ALLOCATE_E =  -9, /* reallocate/allocation failed */
	CDB_ERROR_FREE_E     = -10, /* free failed */
	CDB_ERROR_MODE_E     = -11, /* incorrect mode for operation */
};

typedef struct {
	cdb_word_t position; /* position on disk of this hash table, when known */
	cdb_word_t length;   /* number of buckets in hash table */
} cdb_hash_header_t;

typedef struct {
	cdb_word_t *hashes;       /* full key hashes */
	cdb_word_t *fps;          /* file pointers */
	cdb_hash_header_t header; /* header for this hash table */
} cdb_hash_table_t; /* secondary hash table structure */

struct cdb { /* constant database handle: for all your querying needs! */
	cdb_file_operators_t ops; /* custom file/flash operators */
	cdb_allocator_t a;     /* custom memory allocator routine */
	void *file;            /* database handle */
	cdb_word_t file_start, /* start position of structures in file */ 
	       file_end,       /* end position of database in file, if known, zero otherwise */
	       hash_start;     /* start of secondary hash tables near end of file, if known, zero otherwise */
	cdb_word_t position;   /* key/value file pointer position (creation only) */
	int error;             /* error, if any, any error causes database to be invalid */
	unsigned create  :1,   /* have we opened database up in create mode? */
		 opened  :1,   /* have we successfully opened up the database? */
		 empty   :1;   /* is the database empty? */
	cdb_hash_table_t table1[]; /* only allocated if in create mode, BUCKETS elements are allocated */
};

/* This is not 'djb2' hash - the character is xor'ed in and not added. */
static cdb_word_t hash(uint8_t *s, size_t length) {
	cdb_word_t hash = 5381ul;
	for (size_t i = 0; i < length; i++)
		hash = ((hash << 5ul) + hash) ^ s[i]; /* hash * 33 ^ c */
	return hash;
}

/* void *cdb_get_file(cdb_t *cdb) { assert(cdb); return cdb->file; } */

static inline int cdb_status(cdb_t *cdb) {
	assert(cdb);
	return cdb->error ? CDB_ERROR_E : CDB_OK_E;
}

static inline int cdb_error(cdb_t *cdb, int error) {
	assert(cdb);
	if (cdb->error == 0)
		cdb->error = error;
	return cdb_status(cdb);
}

static inline int cdb_bound(cdb_t *cdb, int fail) {
	assert(cdb);
	return cdb_error(cdb, fail ? CDB_ERROR_BOUND_E : CDB_OK_E);
}

static inline int cdb_hash(cdb_t *cdb, int fail) {
	assert(cdb);
	return cdb_error(cdb, fail ? CDB_ERROR_HASH_E : CDB_OK_E);
}

static inline int cdb_overflow(cdb_t *cdb, int fail) {
	assert(cdb);
	return cdb_error(cdb, fail ? CDB_ERROR_OVERFLOW_E : CDB_OK_E);
}

static inline int cdb_free(cdb_t *cdb, void *p) {
	assert(cdb);
	if (!p)
		return 0;
	//return cdb_error(cdb, cdb->a.free(cdb->a.arena, p) < 0 ? CDB_ERROR_FREE_E : CDB_OK_E);
	return cdb->a.free(cdb->a.arena, p);
}

static inline void *cdb_allocate(cdb_t *cdb, const size_t length) {
	assert(cdb);
	void *r = cdb->a.malloc(cdb->a.arena, length);
	if (length != 0 && r == NULL)
		(void)cdb_error(cdb, CDB_ERROR_ALLOCATE_E);
	return r ? memset(r, 0, length) : NULL;
}

static inline void *cdb_reallocate(cdb_t *cdb, void *pointer, const size_t length) {
	assert(cdb);
	void *r = cdb->a.realloc(cdb->a.arena, pointer, length);
	if (length != 0 && r == NULL)
		(void)cdb_error(cdb, CDB_ERROR_ALLOCATE_E);
	return r;
}

int cdb_seek(cdb_t *cdb, long position, long whence) {
	assert(cdb);
	if (cdb->error)
		return -1;
	unsigned long uposition = position;
	if (cdb->opened && cdb->create == 0) /* TODO: Take into account 'whence' */
		if (cdb_bound(cdb, uposition < cdb->file_start || cdb->file_end < uposition))
			return -1;
	const long r = cdb->ops.seek(cdb->file, position, whence);
	return cdb_error(cdb, r < 0 ? CDB_ERROR_SEEK_E : CDB_OK_E);
}

cdb_word_t cdb_read(cdb_t *cdb, void *buf, size_t length) {
	assert(cdb);
	assert(buf);
	if (cdb->error)
		return 0;
	return cdb->ops.read(cdb->file, buf, length);
}

static cdb_word_t cdb_write(cdb_t *cdb, void *buf, size_t length) {
	assert(cdb);
	assert(buf);
	if (cdb->error)
		return 0;
	return cdb->ops.write(cdb->file, buf, length);
}

int cdb_read_word(cdb_t *cdb, cdb_word_t *word) {
	assert(cdb);
	assert(word);
	uint8_t b[sizeof *word] = { 0 };
	*word = 0;
	const long r = cdb_read(cdb, b, sizeof b);
	if (r != sizeof b)
		return -1;
	cdb_word_t w = 0;
	for (size_t i = 0; i < sizeof w; i++)
		w |= b[i] << (i * CHAR_BIT);
	*word = w;
	return 0;
}

static int cdb_write_word(cdb_t *cdb, const cdb_word_t word) {
	assert(cdb);
	uint8_t b[sizeof word] = { 0 };
	for (size_t i = 0; i < sizeof b; i++)
		b[i] = (word >> (i * CHAR_BIT)) & 0xFFu;
	if (cdb_write(cdb, b, sizeof b) != sizeof b)
		return -1;
	return 0;
}

int cdb_read_word_pair(cdb_t *cdb, cdb_word_t *w1, cdb_word_t *w2) {
	assert(cdb);
	assert(w1);
	assert(w2);
	if (cdb_read_word(cdb, w1) < 0)
		return CDB_ERROR_E;
	return cdb_read_word(cdb, w2);
}

static inline int cdb_write_word_pair(cdb_t *cdb, const cdb_word_t w1, const cdb_word_t w2) {
	assert(cdb);
	if (cdb_write_word(cdb, w1) < 0)
		return -1;
	return cdb_write_word(cdb, w2);
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

static int cdb_free_resources(cdb_t *cdb) {
	if (!cdb)
		return 0;
	if (cdb->file)
		cdb->ops.close(cdb->file);
	cdb->file = NULL;
	cdb->opened = 0;
	cdb_error(cdb, CDB_ERROR_E);
	int r1 = 0;
	for (int i = 0; cdb->create && i < BUCKETS; i++)
		if (cdb_hash_free(cdb, &cdb->table1[i]) < 0)
			r1 = -1;
	const int r2 = cdb_free(cdb, cdb);
	return r1 < 0 || r2 < 0 ? -1 : 0;
}

static int cdb_finalize(cdb_t *cdb) {
	assert(cdb);
	assert(cdb->error == 0);
	assert(cdb->create == 1);
	int r = 0;
	size_t mlen = 8;
	cdb_word_t *hashes    = cdb_allocate(cdb, mlen * sizeof *hashes);
	cdb_word_t *positions = cdb_allocate(cdb, mlen * sizeof *positions);
	if (!hashes || !positions)
		goto fail;
	if (cdb_seek(cdb, cdb->position, CDB_SEEK_START) < 0)
		goto fail;
	cdb->hash_start = cdb->position;

	for (size_t i = 0; i < BUCKETS; i++) {
		cdb_hash_table_t *t = &cdb->table1[i];
		const size_t length = t->header.length * 2ul;
		t->header.position = cdb->position; /* needs to be set */
		if (length == 0)
			continue;
		if (cdb_bound(cdb, length < t->header.length) < 0)
			goto fail;

		if (mlen < length) {
			const size_t required = length * sizeof (cdb_word_t);
			if (cdb_overflow(cdb, required < length) < 0)
				goto fail;
			cdb_word_t *t1 = cdb_reallocate(cdb, hashes, required);
			if (!t1)
				goto fail;
			hashes = t1;
			cdb_word_t *t2 = cdb_reallocate(cdb, positions, required);
			if (!t2)
				goto fail;
			positions = t2;
			mlen = length;
		}
		memset(hashes,    0, length * sizeof (cdb_word_t));
		memset(positions, 0, length * sizeof (cdb_word_t));

		for (size_t j = 0; j < t->header.length; j++) {
			const cdb_word_t h = t->hashes[j];
			const cdb_word_t p = t->fps[j];
			const cdb_word_t start = (h >> 8) % length;
			size_t k = 0;
			for (k = start; positions[k]; k = (k + 1) % length)
				;
			hashes[k]    = h;
			positions[k] = p;
		}

		for (size_t j = 0; j < length; j++)
			if (cdb_write_word_pair(cdb, hashes[j], positions[j]) < 0)
				goto fail;
		
		cdb->position += (length * (2u * sizeof(cdb_word_t)));
	}
	cdb->file_end = cdb->position;
	if (cdb_seek(cdb, cdb->file_start, CDB_SEEK_START) < 0)
		goto fail;
	for (int i = 0; i < BUCKETS; i++) {
		cdb_hash_table_t *t = &cdb->table1[i];
		if (cdb_write_word_pair(cdb, t->header.position, (t->header.length * 2ul)) < 0)
			goto fail;
	}
	if (cdb_free(cdb, hashes) < 0)
		r = -1;
	if (cdb_free(cdb, positions) < 0)
		r = -1;
	return r == 0 && cdb->ops.flush ? cdb->ops.flush(cdb->file) : r;
fail:
	(void)cdb_free(cdb, hashes);
	(void)cdb_free(cdb, positions);
	return cdb_error(cdb, CDB_ERROR_E);
}

int cdb_close(cdb_t *cdb) { /* free cdb, close (and write to disk if in create mode) */
	if (!cdb)
		return 0;
	if (cdb->error)
		goto fail;
	if (cdb->create)
		if (cdb_finalize(cdb) < 0)
			goto fail;
	return cdb_free_resources(cdb);
fail:
	(void)cdb_free_resources(cdb);
	return CDB_ERROR_E;
}

int cdb_open(cdb_t **cdb, cdb_file_operators_t *ops, cdb_allocator_t *allocator, const int create, const char *file) {
	assert(cdb);
	assert(ops);
	assert(ops->read);
	assert(ops->open);
	assert(ops->close);
	assert(ops->seek);
	implies(create, ops->write);
	assert(allocator->malloc);
	assert(allocator->realloc);
	assert(allocator->free);
	/* ops->flush is optional */
	*cdb = NULL;
	cdb_t *c = NULL;
	const int large = MEMORY_INDEX || create;
	const size_t csz = (sizeof *c) + (large * sizeof c->table1[0] * BUCKETS);
	c = allocator->malloc(allocator->arena, csz);
	if (!c)
		goto fail;
	memset(c, 0, csz);
	c->ops        = *ops;
	c->a          = *allocator;
	c->create     = create;
	c->empty      = 1;
	*cdb          = c;
	c->file       = c->ops.open(file, create ? CDB_RW_MODE : CDB_RO_MODE);
	c->file_start = FILE_START;
	if (!(c->file))
		goto fail;
	if (cdb_seek(c, c->file_start, CDB_SEEK_START) < 0)
		goto fail;
	if (create) {
		for (size_t i = 0; i < BUCKETS; i++) /* write empty header */
			if (cdb_write_word_pair(c, 0, 0) < 0)
				goto fail;
	} else {
		/* TODO: We allocate more memory than we need if MEMORY_INDEX is
		 * true as 'cdb_hash_table_t' contains entries needed for
		 * creation that we do not need when reading the database. This
		 * should be fixed. */
		cdb_word_t hpos = 0, hlen = 0, lpos = -1l, lset = 0;
		for (size_t i = 0; i < BUCKETS; i++) {
			cdb_hash_table_t t = { .header = { .position = 0, .length = 0 } };
			if (cdb_read_word_pair(c, &t.header.position, &t.header.length) < 0)
				goto fail;
			if (MEMORY_INDEX)
				c->table1[i] = t;
			if (t.header.length)
				c->empty = 0;
			if (t.header.length && t.header.position < lpos) {
				lpos = t.header.position;
				lset = 1;
			}
			if (t.header.position > hpos) {
				hpos = t.header.position;
				hlen = t.header.length;
			}
		}
		if (cdb_seek(c, c->file_start, CDB_SEEK_START) < 0)
			goto fail;
		c->file_end   = hpos + (hlen * (2ul * sizeof(cdb_word_t)));
		c->hash_start = lset ? lpos : (BUCKETS * (2ul * sizeof(cdb_word_t)));
		if (lset) {
			if (cdb_bound(c, c->file_start > lpos) < 0)
				goto fail;
		}
		if (cdb_overflow(c, c->file_end < hpos) < 0)
			goto fail;
	}
	c->position = c->file_start + (BUCKETS * (2ul * sizeof(cdb_word_t)));
	c->opened = 1;
	return CDB_OK_E;
fail:
	(void)cdb_close(c);
	return CDB_ERROR_E;
}

/* returns: -1 = error, 0 = not equal, 1 = equal */
static int cdb_compare(cdb_t *cdb, const cdb_buffer_t *k1, const cdb_file_pos_t *k2) {
	assert(cdb);
	assert(k1);
	assert(k2);
	/* Note that reading this buffer larger may not make things faster - if
	 * most keys differ in the first few bytes then a small buffer means
	 * less bytes moved around before the comparison. */
	uint8_t kbuf[READ_BUFFER_LENGTH] = { 0 };
	if (k1->length != k2->length)
		return CDB_NOT_FOUND_E; /* not equal */
	const size_t length = k1->length;
	if (cdb_seek(cdb, k2->position, CDB_SEEK_START) < 0)
		return CDB_ERROR_E;
	for (size_t i = 0; i < length; i += sizeof kbuf) {
		const size_t rl = MIN(sizeof kbuf, length - i);
		if (cdb_read(cdb, kbuf, rl) != rl)
			return CDB_ERROR_E;
		if (memcmp(k1->buffer + i, kbuf, rl))
			return CDB_NOT_FOUND_E;
	}
	return CDB_FOUND_E; /* equal */
}

/* returns: -1 = error, 0 = not found, 1 = found */
int cdb_get_record(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value, long record) {
	/* TODO: Make a function to calculate the number of records for a key */
	assert(cdb);
	assert(cdb->opened);
	assert(key);
	assert(value);
	*value = (cdb_file_pos_t) { 0, 0 };
	if (cdb->error)
		goto fail;
	if (cdb->create) {
		(void)cdb_error(cdb, CDB_ERROR_MODE_E);
		goto fail;
	}
	/* locate key in first table */
	const cdb_word_t h = hash((uint8_t *)(key->buffer), key->length);
	cdb_word_t pos = 0, num = 0;

	if (MEMORY_INDEX) { /* use more memory (4KiB) to speed up first match */
		cdb_hash_table_t *t = &cdb->table1[h % BUCKETS];
		pos = t->header.position;
		num = t->header.length;
	} else {
		if (cdb_seek(cdb, cdb->file_start + ((h % BUCKETS) * (2u * sizeof(cdb_word_t))), CDB_SEEK_START) < 0)
			goto fail;
		if (cdb_read_word_pair(cdb, &pos, &num) < 0)
			goto fail;
	}
	if (num == 0) /* no keys in this bucket -> key not found */
		return cdb_status(cdb) < 0 ? CDB_ERROR_E : CDB_NOT_FOUND_E; 
	if (cdb_bound(cdb, pos > cdb->file_end || pos < cdb->hash_start) < 0)
		goto fail;
	const cdb_word_t start = (h >> 8) % num;
	for (size_t i = 0; i < num; i++) {
		const cdb_word_t seekpos = pos + (((start + i) % num) * (2u * sizeof(cdb_word_t)));
		if (seekpos < pos || seekpos > cdb->file_end)
			goto fail;
		if (cdb_seek(cdb, seekpos, CDB_SEEK_START) < 0)
			goto fail;
		cdb_word_t h1 = 0, p1 = 0;
		if (cdb_read_word_pair(cdb, &h1, &p1) < 0)
			goto fail;
		if (cdb_bound(cdb, p1 > cdb->hash_start) < 0) /* key-value pair should not overlap with hash tables section */
			goto fail;
		if (p1 == 0) /* end of list */
			return cdb_status(cdb) < 0 ? CDB_ERROR_E : CDB_NOT_FOUND_E; 
		if (cdb_hash(cdb, (h1 & 0xFFul) != (h & 0xFFul)) < 0) /* buckets bits should be the same */
			goto fail;
		if (h1 == h) { /* possible match */
			if (cdb_seek(cdb, p1, CDB_SEEK_START) < 0)
				goto fail;
			cdb_word_t klen = 0, vlen = 0;
			if (cdb_read_word_pair(cdb, &klen, &vlen) < 0)
				goto fail;
			const cdb_file_pos_t k2 = { .length = klen, .position = p1 + (2u * sizeof(cdb_word_t)) };
			if (cdb_overflow(cdb, k2.position < p1 || (k2.position + klen) < k2.position) < 0)
				goto fail;
			if (cdb_bound(cdb, k2.position + klen > cdb->hash_start) < 0)
				goto fail;
			const int cr = cdb_compare(cdb, key, &k2);
			if (cr < 0)
				goto fail;
			if (cr > 0 && record-- <= 0) { /* found key, correct record? */
				cdb_file_pos_t v2 = { .length = vlen, .position = k2.position + klen };
				if (cdb_overflow(cdb, (v2.position + v2.length) < v2.position) < 0)
					goto fail;
				if (cdb_bound(cdb, v2.position > cdb->hash_start) < 0) 
					goto fail;
				if (cdb_bound(cdb, (v2.position + v2.length) > cdb->hash_start) < 0)
					goto fail;
				*value = v2;
				return cdb_status(cdb) < 0 ? CDB_ERROR_E : CDB_FOUND_E; 
			}
		}
	}
	return cdb_status(cdb) < 0 ? CDB_ERROR_E : CDB_NOT_FOUND_E; 
fail:
	return cdb_error(cdb, CDB_ERROR_E);
}

int cdb_get(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value) {
	assert(cdb);
	assert(cdb->opened);
	assert(key);
	assert(value);
	return cdb_get_record(cdb, key, value, 0l);
}

int cdb_foreach(cdb_t *cdb, cdb_callback cb, void *param) {
	assert(cdb);
	assert(cdb->opened);
	assert(cb);
	if (cdb->error || cdb->create)
		goto fail;
	for (size_t i = 0; i < BUCKETS; i++) {
		const cdb_word_t seekpos = cdb->file_start + (i * (2u * sizeof(cdb_word_t)));
		if (seekpos < cdb->file_start || seekpos > (BUCKETS * (2u * sizeof (cdb_word_t))))
			goto fail;
		if (cdb_seek(cdb, seekpos, CDB_SEEK_START) < 0)
			goto fail;
		cdb_word_t pos = 0, num = 0;
		if (cdb_read_word_pair(cdb, &pos, &num) < 0)
			goto fail;
		if (/*pos < cdb->hash_start ||*/ pos > cdb->file_end)
			goto fail;
		for (size_t j = 0; j < num; j++) {
			if (cdb_seek(cdb, pos + (j * (2u * sizeof(cdb_word_t))), CDB_SEEK_START) < 0)
				goto fail;
			cdb_word_t h1 = 0, p1 = 0;
			if (cdb_read_word_pair(cdb, &h1, &p1) < 0)
				goto fail;
			if (p1 == 0)
				continue;
			if (cdb_hash(cdb, (h1 & 0xFFul) != i) < 0)
				goto fail;
			if (cdb_bound(cdb, (p1 > cdb->hash_start)) < 0)
				goto fail;
			if (cdb_seek(cdb, p1, CDB_SEEK_START) < 0)
				goto fail;
			cdb_word_t klen = 0, vlen = 0;
			if (cdb_read_word_pair(cdb, &klen, &vlen) < 0)
				goto fail;
			const cdb_file_pos_t key   = { .length = klen, .position = p1 + (2u * sizeof(cdb_word_t)), };
		       	const cdb_file_pos_t value = { .length = vlen, .position = p1 + (2u * sizeof(cdb_word_t)) + klen, };
			if (cdb_bound(cdb, value.position > cdb->hash_start) < 0)
				goto fail;
			if (cdb_bound(cdb, (value.position + value.length) > cdb->hash_start) < 0)
				goto fail;
			if (cb(cdb, &key, &value, param) < 0)
				goto fail;
		}
	}
	return cdb_status(cdb);
fail:
	return cdb_error(cdb, CDB_ERROR_E);
}

static int cdb_hash_grow(cdb_t *cdb, cdb_word_t hash, cdb_word_t position) {
	assert(cdb);
	cdb_hash_table_t *t1 = &cdb->table1[hash % BUCKETS];
	cdb_word_t *hashes = cdb_reallocate(cdb, t1->hashes, (t1->header.length + 1) * sizeof (*t1->hashes));
	cdb_word_t *fps    = cdb_reallocate(cdb, t1->fps,    (t1->header.length + 1) * sizeof (*t1->fps));
	if (!hashes || !fps) {
		(void)cdb_hash_free(cdb, t1);
		return CDB_ERROR_E;
	}
	t1->hashes = hashes;
	t1->fps    = fps;
	t1->hashes[t1->header.length] = hash;
	t1->fps[t1->header.length]    = position;
	t1->header.length++;
	return cdb_status(cdb);
}

int cdb_add(cdb_t *cdb, const cdb_buffer_t *key, const cdb_buffer_t *value) {
	assert(cdb);
	assert(cdb->opened);
	assert(key);
	assert(value);
	assert(cdb->position >= cdb->file_start);
	if (cdb->error)
		goto fail;
	if (cdb->create == 0) {
		(void)cdb_error(cdb, CDB_ERROR_MODE_E);
		goto fail;
	}
	const cdb_word_t h = hash((uint8_t*)(key->buffer), key->length);
	if (cdb_seek(cdb, cdb->position, CDB_SEEK_START) < 0)
		goto fail;
	if (cdb_hash_grow(cdb, h, cdb->position) < 0)
		goto fail;
	if (cdb_write_word_pair(cdb, key->length, value->length) < 0)
		goto fail;
	if (cdb_write(cdb, key->buffer, key->length) != key->length)
		goto fail;
	if (cdb_write(cdb, value->buffer, value->length) != value->length)
		goto fail;
	const cdb_word_t add = (2u * sizeof (cdb_word_t)) + key->length + value->length;
	if (cdb_overflow(cdb, key->length + value->length < key->length) < 0)
		goto fail;
	if (cdb_overflow(cdb, (cdb->position + add) <= cdb->position) < 0)
		goto fail;
	cdb->position += add;
	cdb->empty = 0;
	return cdb_status(cdb);
fail:
	return cdb_error(cdb, CDB_ERROR_E);
}

static uint64_t xorshift128(uint64_t s[2]) {
	assert(s);
	if (!s[0] && !s[1])
		s[0] = 1;
	uint64_t a = s[0];
	const uint64_t b = s[1];
	s[0] = b;
	a ^= a << 23;
	a ^= a >> 18;
	a ^= b;
	a ^= b >>  5;
	s[1] = a;
	return a + b;
}

#define VECT (1024ul)
#define KLEN (1024ul)
#define VLEN (1024ul)

/* TODO: More tests -- invalid files, ... */
int cdb_tests(cdb_file_operators_t *ops, cdb_allocator_t *allocator, const char *test_file) {
	assert(ops);
	assert(allocator);
	assert(test_file);
	BUILD_BUG_ON(sizeof (cdb_word_t) < 2);

	if (!DEBUGGING)
		return CDB_OK_E;

	typedef struct {
		char key[KLEN], value[VLEN], result[VLEN];
		cdb_word_t klen, vlen;
	} test_t;

	typedef struct { char *key, *value; int record; } test_duplicate_t;

	/* "dups" should not overlap with randomly generated test-cases */
	static const test_duplicate_t dups[] = {
		{ "ALPHA", "BRAVO",   0 },
		{ "ALPHA", "CHARLIE", 1 },
		{ "ALPHA", "DELTA",   2 },
		{ "1234",  "5678",    0 },
		{ "1234",  "9ABC",    1 },
		{ "",      "",        0 },
		{ "",      "X",       1 },
		{ "",      "",        2 },
	};
	const size_t dupcnt = sizeof (dups) / sizeof (dups[0]);

	cdb_t *cdb = NULL;
	test_t *ts = NULL;
	uint64_t s[2] = { 0 };
	int r = CDB_OK_E;

	if (cdb_open(&cdb, ops, allocator, 1, test_file) < 0)
		return CDB_ERROR_E;
	if (!(ts = cdb_allocate(cdb, VECT * (sizeof *ts))))
		goto fail;

	/* NB. We cannot deal with duplicate keys in this test, the chosen
	 * PRNG happens to not generate any. Do not change it. */
	for (unsigned i = 0; i < VECT; i++) {
		char *k = ts[i].key;
		char *v = ts[i].value;
		const unsigned kl = (xorshift128(s) % (KLEN - 1ul)) + 1ul;
		const unsigned vl = (xorshift128(s) % (VLEN - 1ul)) + 1ul;
		for (unsigned j = 0; j < kl; j++)
			k[j] = 'a' + (xorshift128(s) % 26);
		for (unsigned j = 0; j < vl; j++)
			v[j] = 'a' + (xorshift128(s) % 26);
		const cdb_buffer_t key   = { .length = kl, .buffer = k };
	       	const cdb_buffer_t value = { .length = vl, .buffer = v };
		if (cdb_add(cdb, &key, &value) < 0)
			goto fail;
		ts[i].klen = kl;
		ts[i].vlen = vl;
	}

	for (size_t i = 0; i < dupcnt; i++) {
		test_duplicate_t d = dups[i];
		const cdb_buffer_t key   = { .length = strlen(d.key),   .buffer = d.key };
		const cdb_buffer_t value = { .length = strlen(d.value), .buffer = d.value };
		if (cdb_add(cdb, &key, &value) < 0)
			goto fail;
	}

	if (cdb_close(cdb) < 0) {
		(void)allocator->free(allocator->arena, ts);
		return -1;
	}
	cdb = NULL;

	if (cdb_open(&cdb, ops, allocator, 0, test_file) < 0) {
		(void)allocator->free(allocator->arena, ts);
		return -1;
	}

	for (unsigned i = 0; i < VECT; i++) {
		test_t *t = &ts[i];
		const cdb_buffer_t key = { .length = t->klen, .buffer = t->key };
		cdb_file_pos_t result = { 0, 0 };
		const int g = cdb_get(cdb, &key, &result);
		if (g < 0)
			goto fail;
		if (g == 0) {
			r = -2;
			continue;
		}

		if (result.length > VLEN)
			goto fail;
		if (result.length != t->vlen) {
			r = -3;
		} else {
			if (cdb_seek(cdb, result.position, CDB_SEEK_START) < 0)
				goto fail;
			if (cdb_read(cdb, t->result, result.length) != result.length)
				goto fail;
			if (memcmp(t->result, t->value, result.length))
				r = -4;
		}
	}

	for (size_t i = 0; i < dupcnt; i++) {
		test_duplicate_t d = dups[i];
		const cdb_buffer_t key   = { .length = strlen(d.key),   .buffer = d.key };
		const cdb_buffer_t value = { .length = strlen(d.value), .buffer = d.value }; 
		cdb_file_pos_t result = { 0, 0 };
		const int g = cdb_get_record(cdb, &key, &result, d.record);
		if (g < 0)
			goto fail;
		if (g == 0) {
			r = -5;
			continue;
		}
		const int cmp = cdb_compare(cdb, &value, &result);
		if (cmp < 0)
			goto fail;
		if (cmp != 1)
			r = -6;
	}

	if (cdb_free(cdb, ts) < 0)
		r = -1;
	if (cdb_close(cdb) < 0)
		r = -1;
	return r;
fail:
	(void)allocator->free(allocator->arena, ts);
	(void)cdb_close(cdb);
	return CDB_ERROR_E;
}

