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

#ifndef CDB_SIZE
#define CDB_SIZE (32ul)
#endif

#ifndef CDB_VERSION
#define CDB_VERSION (0x000000ul) /* all zeros = built incorrectly */
#endif

#ifndef CDB_TESTS_ON
#define CDB_TESTS_ON (1)
#endif

#ifndef CDB_WRITE_ON
#define CDB_WRITE_ON (1)
#endif

#ifndef CDB_MEMORY_INDEX_ON /* use in memory hash table if '1' for first table */
#define CDB_MEMORY_INDEX_ON (0)
#endif

#define BUILD_BUG_ON(condition)   ((void)sizeof(char[1 - 2*!!(condition)]))
#define implies(P, Q)             assert(!(P) || (Q))
#define MIN(X, Y)                 ((X) < (Y) ? (X) : (Y))
#define FILE_START                (0ul)
#define NBUCKETS                  (8ul)
#define BUCKETS                   (1ul << NBUCKETS)
#define READ_BUFFER_LENGTH        (256ul)

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
	CDB_ERROR_DISABLED_E = -12, /* unimplemented/disabled feature */
};

typedef struct {
	cdb_word_t position; /* position on disk of this hash table, when known */
	cdb_word_t length;   /* number of buckets in hash table */
} cdb_hash_header_t; /* initial hash table structure */

typedef struct {
	cdb_word_t *hashes;       /* full key hashes */
	cdb_word_t *fps;          /* file pointers */
	cdb_hash_header_t header; /* header for this hash table */
} cdb_hash_table_t; /* secondary hash table structure */

struct cdb { /* constant database handle: for all your querying needs! */
	cdb_callbacks_t ops; /* custom file/flash operators */
	void *file;            /* database handle */
	void *arena;           /* arena allocator */
	cdb_word_t file_start, /* start position of structures in file */
	       file_end,       /* end position of database in file, if known, zero otherwise */
	       hash_start;     /* start of secondary hash tables near end of file, if known, zero otherwise */
	cdb_word_t position;   /* key/value file pointer position (creation only) */
	int error;             /* error, if any, any error causes database to be invalid */
	unsigned create  :1,   /* have we opened database up in create mode? */
		 opened  :1,   /* have we successfully opened up the database? */
		 empty   :1,   /* is the database empty? */
		 dirty   :1;   /* has the file position changed by an external entity? */
	cdb_hash_table_t table1[]; /* only allocated if in create mode, BUCKETS elements are allocated */
};

/* To make the library easier to use we could provide a set of default
 * allocators (that when compiled out always return an error), the non-thread
 * safe allocator would return a pointer to a statically declared variable and
 * mark it as being used. */

int cdb_get_version(unsigned long *version) {
	BUILD_BUG_ON(CDB_SIZE != 16 && CDB_SIZE != 32 && CDB_SIZE != 64);
	BUILD_BUG_ON((sizeof (cdb_word_t) * CHAR_BIT) != CDB_SIZE);
	assert(version);
	unsigned long spec = CDB_SIZE >> 4; /* Lowest three bits = size */
	spec |= CDB_TESTS_ON        << 4;
	spec |= CDB_WRITE_ON        << 5;
	spec |= CDB_MEMORY_INDEX_ON << 6;
	/*spec |= 0                 << 7; */
	*version = (spec << 24) | CDB_VERSION;
	return CDB_VERSION == 0 ? CDB_ERROR_E : CDB_OK_E;
}

/* A function for resolving an error code into a (constant and static) string
 * would help, but is not needed. It is just more bloat. */
int cdb_get_error(cdb_t *cdb) {
	assert(cdb);
	return cdb->error;
}

/* This is not 'djb2' hash - the character is xor'ed in and not added. */
static uint32_t djb_hash(const uint8_t *s, const size_t length) {
	assert(s);
	uint32_t h = 5381ul;
	for (cdb_word_t i = 0; i < length; i++)
		h = ((h << 5ul) + h) ^ s[i]; /* (h * 33) xor c */
	return h;
}

static inline int cdb_status(cdb_t *cdb) {
	assert(cdb);
	return cdb->error ? CDB_ERROR_E : CDB_OK_E;
}

static inline int cdb_error(cdb_t *cdb, const int error) {
	assert(cdb);
	if (cdb->error == 0)
		cdb->error = error;
	return cdb_status(cdb);
}

static inline int cdb_bound_check(cdb_t *cdb, const int fail) {
	assert(cdb);
	return cdb_error(cdb, fail ? CDB_ERROR_BOUND_E : CDB_OK_E);
}

static inline int cdb_hash_check(cdb_t *cdb, const int fail) {
	assert(cdb);
	return cdb_error(cdb, fail ? CDB_ERROR_HASH_E : CDB_OK_E);
}

static inline int cdb_overflow_check(cdb_t *cdb, const int fail) {
	assert(cdb);
	return cdb_error(cdb, fail ? CDB_ERROR_OVERFLOW_E : CDB_OK_E);
}

static void cdb_assert(cdb_t *cdb) {
	assert(cdb);
	implies(cdb->file_end   != 0, cdb->file_end   > cdb->file_start);
	implies(cdb->hash_start != 0, cdb->hash_start > cdb->file_start);
	assert(cdb->ops.allocator);
	assert(cdb->ops.read);
	assert(cdb->ops.open);
	assert(cdb->ops.close);
	assert(cdb->ops.seek);
	implies(cdb->create, cdb->ops.write);
	/*assert(cdb->error == 0);*/
}

static inline int cdb_free(cdb_t *cdb, void *p) {
	assert(cdb);
	if (!p)
		return 0;
	(void)cdb->ops.allocator(cdb->arena, p, 0, 0);
	return 0;
}

static inline void *cdb_allocate(cdb_t *cdb, const size_t length) {
	assert(cdb);
	void *r = cdb->ops.allocator(cdb->arena, NULL, 0, length);
	if (length != 0 && r == NULL)
		(void)cdb_error(cdb, CDB_ERROR_ALLOCATE_E);
	return r ? memset(r, 0, length) : NULL;
}

static inline void *cdb_reallocate(cdb_t *cdb, void *pointer, const size_t length) {
	assert(cdb);
	void *r = cdb->ops.allocator(cdb->arena, pointer, 0, length);
	if (length != 0 && r == NULL)
		(void)cdb_error(cdb, CDB_ERROR_ALLOCATE_E);
	return r;
}

/* NB. A seek can cause buffers to be flushed, which generally degrades
 * performance quite a lot */
static int cdb_seek_internal(cdb_t *cdb, const cdb_word_t position, const int whence) {
	cdb_assert(cdb);
	if (cdb->error)
		return -1;
	if (cdb->opened && cdb->create == 0 && whence == CDB_SEEK_START) /* other seek methods not checked */
		if (cdb_bound_check(cdb, position < cdb->file_start || cdb->file_end < position))
			return -1;
	const long r = cdb->ops.seek(cdb->file, position, whence);
	return cdb_error(cdb, r < 0 ? CDB_ERROR_SEEK_E : CDB_OK_E);
}

int cdb_seek(cdb_t *cdb, const cdb_word_t position, const int whence) {
	cdb_assert(cdb);
	cdb->dirty = 1;
	if (cdb_error(cdb, cdb->create != 0 ? CDB_ERROR_MODE_E : 0))
		return 0;
	return cdb_seek_internal(cdb, position, whence);
}

static cdb_word_t cdb_read_internal(cdb_t *cdb, void *buf, cdb_word_t length) {
	cdb_assert(cdb);
	assert(buf);
	if (cdb_error(cdb, cdb->create != 0 ? CDB_ERROR_MODE_E : 0))
		return 0;
	return cdb->ops.read(cdb->file, buf, length);
}

int cdb_read(cdb_t *cdb, void *buf, cdb_word_t length) {
	cdb_assert(cdb);
	cdb->dirty = 1;
	const cdb_word_t r = cdb_read_internal(cdb, buf, length);
	return cdb_error(cdb, r != length ? CDB_ERROR_READ_E : 0);
}

static cdb_word_t cdb_write(cdb_t *cdb, void *buf, size_t length) {
	assert(buf);
	cdb_assert(cdb);
	cdb->dirty = 1;
	if (cdb_error(cdb, cdb->create == 0 ? CDB_ERROR_MODE_E : 0))
		return 0;
	return cdb->ops.write(cdb->file, buf, length);
}

static inline void cdb_pack(uint8_t b[/*static (sizeof (cdb_word_t))*/], cdb_word_t w) {
	assert(b);
	for (size_t i = 0; i < sizeof w; i++)
		b[i] = (w >> (i * CHAR_BIT)) & 0xFFu;
}

static inline cdb_word_t cdb_unpack(uint8_t b[/*static (sizeof (cdb_word_t))*/]) {
	assert(b);
	cdb_word_t w = 0;
	for (size_t i = 0; i < sizeof w; i++)
		w |= ((cdb_word_t)b[i]) << (i * CHAR_BIT);
	return w;
}

int cdb_read_word_pair(cdb_t *cdb, cdb_word_t *w1, cdb_word_t *w2) {
	assert(cdb);
	assert(w1);
	assert(w2);
	uint8_t b[2ul * sizeof(cdb_word_t)];
	const long r = cdb_read_internal(cdb, b, sizeof b);
	if (r != sizeof b)
		return -1;
	*w1 = cdb_unpack(b);
	*w2 = cdb_unpack(b + sizeof(cdb_word_t));
	return 0;
}

static int cdb_write_word_pair(cdb_t *cdb, const cdb_word_t w1, const cdb_word_t w2) {
	assert(cdb);
	uint8_t b[2ul * sizeof(cdb_word_t)];
	cdb_pack(b, w1);
	cdb_pack(b + sizeof(cdb_word_t), w2);
	if (cdb_write(cdb, b, sizeof b) != sizeof b)
		return -1;
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

static int cdb_free_resources(cdb_t *cdb) {
	if (!cdb)
		return 0;
	if (cdb->file)
		cdb->ops.close(cdb->file);
	cdb->file = NULL;
	cdb->opened = 0;
	int r = 0;
	for (size_t i = 0; cdb->create && i < BUCKETS; i++)
		if (cdb_hash_free(cdb, &cdb->table1[i]) < 0)
			r = -1;
	(void)cdb_error(cdb, CDB_ERROR_E);
	(void)cdb->ops.allocator(cdb->arena, cdb, 0, 0);
	return r;
}

static inline int cdb_finalize(cdb_t *cdb) { /* write hash tables to disk */
	assert(cdb);
	assert(cdb->error == 0);
	assert(cdb->create == 1);
	if (CDB_WRITE_ON == 0)
		return cdb_error(cdb, CDB_ERROR_DISABLED_E);
	int r = 0;
	cdb_word_t mlen = 8;
	cdb_word_t *hashes    = cdb_allocate(cdb, mlen * sizeof *hashes);
	cdb_word_t *positions = cdb_allocate(cdb, mlen * sizeof *positions);
	if (!hashes || !positions)
		goto fail;
	if (cdb_seek_internal(cdb, cdb->position, CDB_SEEK_START) < 0)
		goto fail;
	cdb->hash_start = cdb->position;

	for (size_t i = 0; i < BUCKETS; i++) { /* write tables at end of file */
		cdb_hash_table_t *t = &cdb->table1[i];
		const cdb_word_t length = t->header.length * 2ul;
		t->header.position = cdb->position; /* needs to be set */
		if (length == 0)
			continue;
		if (cdb_bound_check(cdb, length < t->header.length) < 0)
			goto fail;

		if (mlen < length) {
			const cdb_word_t required = length * sizeof (cdb_word_t);
			if (cdb_overflow_check(cdb, required < length) < 0)
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
			const cdb_word_t start = (h >> NBUCKETS) % length;
			cdb_word_t k = 0;
			for (k = start; positions[k]; k = (k + 1ul) % length)
				;
			hashes[k]    = h;
			positions[k] = p;
		}

		for (cdb_word_t j = 0; j < length; j++)
			if (cdb_write_word_pair(cdb, hashes[j], positions[j]) < 0)
				goto fail;
	
		cdb->position += (length * (2ul * sizeof(cdb_word_t)));
	}
	cdb->file_end = cdb->position;
	if (cdb_seek_internal(cdb, cdb->file_start, CDB_SEEK_START) < 0)
		goto fail;
	for (size_t i = 0; i < BUCKETS; i++) { /* write initial hash table */
		const cdb_hash_table_t * const t = &cdb->table1[i];
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

int cdb_open(cdb_t **cdb, const cdb_callbacks_t *ops, void *arena, const int create, const char *file) {
	/* We could allow the word size of the CDB database {16, 32 (default) or 64}
	 * to be configured at run time and not compile time, this has API related
	 * consequences, the size of 'cdb_word_t' would determine maximum size that
	 * could be supported by this library. 'cdb_open' would have to take another
	 * parameter or one of the structures passed in would need to be extended. */
	assert(cdb);
	assert(ops);
	assert(ops->allocator);
	assert(ops->read);
	assert(ops->open);
	assert(ops->close);
	assert(ops->seek);
	implies(create, ops->write);
	/* ops->flush is optional */
	*cdb = NULL;
	if (create && CDB_WRITE_ON == 0)
		return CDB_ERROR_E;
	cdb_t *c = NULL;
	const int large = CDB_MEMORY_INDEX_ON || create;
	const size_t csz = (sizeof *c) + (large * sizeof c->table1[0] * BUCKETS);
	c = ops->allocator(arena, NULL, 0, csz);
	if (!c)
		goto fail;
	memset(c, 0, csz);
	c->ops        = *ops;
	c->arena      = arena;
	c->create     = create;
	c->empty      = 1;
	*cdb          = c;
	c->file       = c->ops.open(file, create ? CDB_RW_MODE : CDB_RO_MODE);
	c->file_start = FILE_START;
	if (!(c->file)) {
		(void)cdb_error(c, CDB_ERROR_OPEN_E);
		goto fail;
	}
	if (cdb_seek_internal(c, c->file_start, CDB_SEEK_START) < 0)
		goto fail;
	if (create) {
		for (size_t i = 0; i < BUCKETS; i++) /* write empty header */
			if (cdb_write_word_pair(c, 0, 0) < 0)
				goto fail;
	} else {
		/* We allocate more memory than we need if CDB_MEMORY_INDEX_ON is
		 * true as 'cdb_hash_table_t' contains entries needed for
		 * creation that we do not need when reading the database. */
		cdb_word_t hpos = 0, hlen = 0, lpos = -1l, lset = 0, prev = 0, pnum = 0;
		for (size_t i = 0; i < BUCKETS; i++) {
			cdb_hash_table_t t = { .header = { .position = 0, .length = 0 } };
			if (cdb_read_word_pair(c, &t.header.position, &t.header.length) < 0)
				goto fail;
			if (i && t.header.position != (prev + (pnum * (2ul * sizeof (cdb_word_t)))))
				goto fail;
			prev = t.header.position;
			pnum = t.header.length;
			if (CDB_MEMORY_INDEX_ON)
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
		if (cdb_seek_internal(c, c->file_start, CDB_SEEK_START) < 0)
			goto fail;
		c->file_end   = hpos + (hlen * (2ul * sizeof(cdb_word_t)));
		c->hash_start = lset ? lpos : (BUCKETS * (2ul * sizeof(cdb_word_t)));
		if (lset) {
			if (cdb_bound_check(c, c->file_start > lpos) < 0)
				goto fail;
		}
		if (cdb_overflow_check(c, c->file_end < hpos) < 0)
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
	if (k1->length != k2->length)
		return CDB_NOT_FOUND_E; /* not equal */
	const cdb_word_t length = k1->length;
	if (cdb_seek_internal(cdb, k2->position, CDB_SEEK_START) < 0)
		return CDB_ERROR_E;
	for (cdb_word_t i = 0; i < length; i += READ_BUFFER_LENGTH) {
		/* Note that making this buffer larger may not make things faster - if
		 * most keys differ in the first few bytes then a smaller buffer means
		 * less bytes moved around before the comparison. */
		uint8_t kbuf[READ_BUFFER_LENGTH];
		BUILD_BUG_ON(sizeof kbuf != READ_BUFFER_LENGTH);
		const cdb_word_t rl = MIN((cdb_word_t)sizeof kbuf, (cdb_word_t)length - i);
		if (cdb_read_internal(cdb, kbuf, rl) != rl)
			return CDB_ERROR_E;
		if (memcmp(k1->buffer + i, kbuf, rl))
			return CDB_NOT_FOUND_E;
	}
	return CDB_FOUND_E; /* equal */
}

static int cdb_retrieve(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value, long *record) {
	assert(cdb);
	assert(cdb->opened);
	assert(key);
	assert(value);
	assert(record);
	long wanted = *record, recno = 0;
	*record = 0;
	*value = (cdb_file_pos_t) { 0, 0 };
	if (cdb->error)
		goto fail;
	if (cdb->create) {
		(void)cdb_error(cdb, CDB_ERROR_MODE_E);
		goto fail;
	}
	/* locate key in first table */
	const cdb_word_t h = djb_hash((uint8_t *)(key->buffer), key->length);
	cdb_word_t pos = 0, num = 0;

	if (CDB_MEMORY_INDEX_ON) { /* use more memory (~4KiB) to speed up first match */
		cdb_hash_table_t *t = &cdb->table1[h % BUCKETS];
		pos = t->header.position;
		num = t->header.length;
	} else {
		if (cdb_seek_internal(cdb, cdb->file_start + ((h % BUCKETS) * (2ul * sizeof(cdb_word_t))), CDB_SEEK_START) < 0)
			goto fail;
		if (cdb_read_word_pair(cdb, &pos, &num) < 0)
			goto fail;
	}
	if (num == 0) /* no keys in this bucket -> key not found */
		return cdb_status(cdb) < 0 ? CDB_ERROR_E : CDB_NOT_FOUND_E;
	if (cdb_bound_check(cdb, pos > cdb->file_end || pos < cdb->hash_start) < 0)
		goto fail;
	const cdb_word_t start = (h >> NBUCKETS) % num;
	for (cdb_word_t i = 0; i < num; i++) {
		const cdb_word_t seekpos = pos + (((start + i) % num) * (2ul * sizeof(cdb_word_t)));
		if (seekpos < pos || seekpos > cdb->file_end)
			goto fail;
		if (cdb_seek_internal(cdb, seekpos, CDB_SEEK_START) < 0)
			goto fail;
		cdb_word_t h1 = 0, p1 = 0;
		if (cdb_read_word_pair(cdb, &h1, &p1) < 0)
			goto fail;
		if (cdb_bound_check(cdb, p1 > cdb->hash_start) < 0) /* key-value pair should not overlap with hash tables section */
			goto fail;
		if (p1 == 0) /* end of list */ {
			*record = recno;
			return cdb_status(cdb) < 0 ? CDB_ERROR_E : CDB_NOT_FOUND_E;
		}
		if (cdb_hash_check(cdb, (h1 & 0xFFul) != (h & 0xFFul)) < 0) /* buckets bits should be the same */
			goto fail;
		if (h1 == h) { /* possible match */
			if (cdb_seek_internal(cdb, p1, CDB_SEEK_START) < 0)
				goto fail;
			cdb_word_t klen = 0, vlen = 0;
			if (cdb_read_word_pair(cdb, &klen, &vlen) < 0)
				goto fail;
			const cdb_file_pos_t k2 = { .length = klen, .position = p1 + (2ul * sizeof(cdb_word_t)) };
			if (cdb_overflow_check(cdb, k2.position < p1 || (k2.position + klen) < k2.position) < 0)
				goto fail;
			if (cdb_bound_check(cdb, k2.position + klen > cdb->hash_start) < 0)
				goto fail;
			const int cr = cdb_compare(cdb, key, &k2);
			if (cr < 0)
				goto fail;
			if (cr > 0 && recno == wanted) { /* found key, correct record? */
				cdb_file_pos_t v2 = { .length = vlen, .position = k2.position + klen };
				if (cdb_overflow_check(cdb, (v2.position + v2.length) < v2.position) < 0)
					goto fail;
				if (cdb_bound_check(cdb, v2.position > cdb->hash_start) < 0)
					goto fail;
				if (cdb_bound_check(cdb, (v2.position + v2.length) > cdb->hash_start) < 0)
					goto fail;
				*record = recno;
				*value = v2;
				return cdb_status(cdb) < 0 ? CDB_ERROR_E : CDB_FOUND_E;
			}
			recno++;
		}
	}
	*record = recno;
	return cdb_status(cdb) < 0 ? CDB_ERROR_E : CDB_NOT_FOUND_E;
fail:
	return cdb_error(cdb, CDB_ERROR_E);
}

int cdb_get_record(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value, long record) {
	assert(cdb);
	assert(cdb->opened);
	assert(key);
	assert(value);
	return cdb_retrieve(cdb, key, value, &record);
}

int cdb_get(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value) {
	assert(cdb);
	assert(cdb->opened);
	assert(key);
	assert(value);
	return cdb_get_record(cdb, key, value, 0l);
}

/* Missing from the API is a way of retrieving keys in a more efficient
 * manner. Retrieving the Nth key means cycling through prior keys, by
 * storing a little state in 'cdb_t' we could support this. */

int cdb_get_count(cdb_t *cdb, const cdb_buffer_t *key, long *count) {
	assert(cdb);
	assert(cdb->opened);
	assert(key);
	assert(count);
	cdb_file_pos_t value = { 0, 0 };
	long c = LONG_MAX;
	const int r = cdb_retrieve(cdb, key, &value, &c);
	c = r == CDB_FOUND_E ? c + 1l : c;
	*count = c;
	return r;
}

int cdb_foreach(cdb_t *cdb, cdb_callback cb, void *param) {
	assert(cdb);
	assert(cdb->opened);
	assert(cb);
	if (cdb->error || cdb->create)
		goto fail;
	cdb_word_t pos = cdb->file_start + (256ul * (2ul * sizeof (cdb_word_t)));
	if (cdb_seek_internal(cdb, pos, CDB_SEEK_START) < 0)
		goto fail;
	for (;pos < cdb->hash_start;) {
		if (cdb_seek_internal(cdb, pos, CDB_SEEK_START) < 0)
			goto fail;
		cdb->dirty = 0;
		cdb_word_t klen = 0, vlen = 0;
		if (cdb_read_word_pair(cdb, &klen, &vlen) < 0)
			goto fail;
		const cdb_file_pos_t key   = { .length = klen, .position = pos + (2ul * sizeof(cdb_word_t)), };
		const cdb_file_pos_t value = { .length = vlen, .position = pos + (2ul * sizeof(cdb_word_t)) + klen, };
		if (cdb_bound_check(cdb, value.position > cdb->hash_start) < 0)
			goto fail;
		if (cdb_bound_check(cdb, (value.position + value.length) > cdb->hash_start) < 0)
			goto fail;
		if (cb(cdb, &key, &value, param) < 0)
			goto fail;
		pos = value.position + value.length;
	}
	return cdb_status(cdb);
fail:
	return cdb_error(cdb, CDB_ERROR_E);
}

static int round_up(const cdb_word_t x) {
	cdb_word_t p = 1ul;
	while (p < x)
		p <<= 1ul;
	return p;
}

static int cdb_hash_grow(cdb_t *cdb, const cdb_word_t hash, const cdb_word_t position) {
	assert(cdb);
	cdb_hash_table_t *t1 = &cdb->table1[hash % BUCKETS];
	cdb_word_t *hashes = t1->hashes, *fps = t1->fps;
	const cdb_word_t next = round_up(t1->header.length + 1ul);
	const cdb_word_t cur  = round_up(t1->header.length);
	if (next > cur || t1->header.length == 0) {
		const cdb_word_t alloc = t1->header.length == 0 ? 1ul : t1->header.length * 2ul;
		if (!(hashes = cdb_reallocate(cdb, t1->hashes, alloc * sizeof (*t1->hashes))))
			return CDB_ERROR_E;
		t1->hashes = hashes;
		if (!(fps = cdb_reallocate(cdb, t1->fps, alloc * sizeof (*t1->fps)))) {
			(void)cdb_hash_free(cdb, t1);
			return CDB_ERROR_E;
		}
	}
	t1->hashes = hashes;
	t1->fps    = fps;
	t1->hashes[t1->header.length] = hash;
	t1->fps[t1->header.length]    = position;
	t1->header.length++;
	return cdb_status(cdb);
}

int cdb_add(cdb_t *cdb, const cdb_buffer_t *key, const cdb_buffer_t *value) {
	cdb_assert(cdb);
	assert(cdb->opened);
	assert(key);
	assert(value);
	assert(cdb->position >= cdb->file_start);
	if (CDB_WRITE_ON == 0)
		return cdb_error(cdb, CDB_ERROR_DISABLED_E);
	if (cdb->error)
		goto fail;
	if (cdb->create == 0) {
		(void)cdb_error(cdb, CDB_ERROR_MODE_E);
		goto fail;
	}
	const cdb_word_t h = djb_hash((uint8_t*)(key->buffer), key->length);
	if (cdb_hash_grow(cdb, h, cdb->position) < 0)
		goto fail;
	if (cdb->dirty)
		if (cdb_seek_internal(cdb, cdb->position, CDB_SEEK_START) < 0)
			goto fail;
	cdb->dirty = 0;
	if (cdb_write_word_pair(cdb, key->length, value->length) < 0)
		goto fail;
	if (cdb_write(cdb, key->buffer, key->length) != key->length)
		goto fail;
	if (cdb_write(cdb, value->buffer, value->length) != value->length)
		goto fail;
	const cdb_word_t add = key->length + value->length + (2ul * sizeof (cdb_word_t));
	if (cdb_overflow_check(cdb, key->length + value->length < key->length) < 0)
		goto fail;
	if (cdb_overflow_check(cdb, (cdb->position + add) <= cdb->position) < 0)
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

#if defined(CDB_SIZE) && CDB_SIZE == 16
#define VECT (128ul)
#define KLEN (64ul)
#define VLEN (64ul)
#else
#define VECT (1024ul)
#define KLEN (1024ul)
#define VLEN (1024ul)
#endif

int cdb_tests(const cdb_callbacks_t *ops, void *arena, const char *test_file) {
	assert(ops);
	assert(test_file);
	BUILD_BUG_ON(sizeof (cdb_word_t) < 2);

	if (CDB_TESTS_ON == 0)
		return CDB_OK_E;

	typedef struct {
		char key[KLEN], value[VLEN], result[VLEN];
		long recno;
		cdb_word_t klen, vlen;
	} test_t;

	typedef struct { char *key, *value; } test_duplicate_t;

	static const test_duplicate_t dups[] = { /* add known duplicates */
		{ "ALPHA", "BRAVO",   },
		{ "ALPHA", "CHARLIE", },
		{ "ALPHA", "DELTA",   },
		{ "1234",  "5678",    },
		{ "1234",  "9ABC",    },
		{ "",      "",        },
		{ "",      "X",       },
		{ "",      "",        },
	};
	const size_t dupcnt = sizeof (dups) / sizeof (dups[0]);

	cdb_t *cdb = NULL;
	test_t *ts = NULL;
	uint64_t s[2] = { 0 };
	int r = CDB_OK_E;

	if (cdb_open(&cdb, ops, arena, 1, test_file) < 0)
		return CDB_ERROR_E;
	if (!(ts = cdb_allocate(cdb, (dupcnt + VECT) * (sizeof *ts))))
		goto fail;

	for (unsigned i = 0; i < VECT; i++) {
		char *k = ts[i].key;
		char *v = ts[i].value;
		const cdb_word_t kl = (xorshift128(s) % (KLEN - 1ul)) + 1ul;
		const cdb_word_t vl = (xorshift128(s) % (VLEN - 1ul)) + 1ul;
		for (unsigned long j = 0; j < kl; j++)
			k[j] = 'a' + (xorshift128(s) % 26);
		for (unsigned long j = 0; j < vl; j++)
			v[j] = 'a' + (xorshift128(s) % 26);
		const cdb_buffer_t key   = { .length = kl, .buffer = k };
	       	const cdb_buffer_t value = { .length = vl, .buffer = v };
		for (unsigned long j = 0; j < i; j++)
			if (memcmp(ts[i].value, ts[j].value, VLEN) == 0)
				ts[i].recno++;
		if (cdb_add(cdb, &key, &value) < 0)
			goto fail;
		ts[i].klen = kl;
		ts[i].vlen = vl;
	}

	for (size_t i = 0; i < dupcnt; i++) {
		test_duplicate_t d = dups[i];
		const cdb_buffer_t key   = { .length = strlen(d.key),   .buffer = d.key };
		const cdb_buffer_t value = { .length = strlen(d.value), .buffer = d.value };

		memcpy(ts[i + VECT].key,   key.buffer,   key.length);
		memcpy(ts[i + VECT].value, value.buffer, value.length);

		for (unsigned long j = 0; j < i; j++)
			if (memcmp(ts[i].value, ts[j].value, VLEN) == 0)
				ts[i].recno++;

		if (cdb_add(cdb, &key, &value) < 0)
			goto fail;
	}

	if (cdb_close(cdb) < 0) {
		(void)ops->allocator(arena, ts, 0, 0);
		return -1;
	}
	cdb = NULL;

	if (cdb_open(&cdb, ops, arena, 0, test_file) < 0) {
		(void)ops->allocator(arena, ts, 0, 0);
		return -1;
	}

	for (unsigned i = 0; i < (VECT + dupcnt); i++) {
		test_t *t = &ts[i];
		const cdb_buffer_t key = { .length = t->klen, .buffer = t->key };
		cdb_file_pos_t result = { 0, 0 }, discard = { 0, 0 };
		const int g = cdb_get_record(cdb, &key, &result, t->recno);
		if (g < 0)
			goto fail;
		if (g == CDB_NOT_FOUND_E) {
			r = -3; /* -2 not used */
			continue;
		}

		const int d = cdb_get(cdb, &key, &discard);
		if (d < 0)
			goto fail;
		if (d == CDB_NOT_FOUND_E)
			r = -4;

		if (result.length > VLEN)
			goto fail;
		if (result.length != t->vlen) {
			r = -5;
		} else {
			if (cdb_seek_internal(cdb, result.position, CDB_SEEK_START) < 0)
				goto fail;
			if (cdb_read_internal(cdb, t->result, result.length) != result.length)
				goto fail;
			if (memcmp(t->result, t->value, result.length))
				r = -6;
		}

		long cnt = 0;
		if (cdb_get_count(cdb, &key, &cnt) < 0)
			goto fail;
		if (cnt < t->recno)
			r = -7;
	}

	if (cdb_free(cdb, ts) < 0)
		r = -1;
	if (cdb_close(cdb) < 0)
		r = -1;
	return r;
fail:
	(void)ops->allocator(arena, ts, 0, 0);
	(void)cdb_close(cdb);
	return CDB_ERROR_E;
}

