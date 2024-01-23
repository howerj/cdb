/* Program: Constant Database Library
 * Author:  Richard James Howe
 * Email:   howe.r.j.89@gmail.com
 * License: Unlicense
 * Repo:    <https://github.com/howerj/cdb>
 *
 * Consult the "readme.md" file for a detailed description
 * of the file format and internals. */

#include "cdb.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#ifndef CDB_VERSION
#define CDB_VERSION (0x000000ul) /* all zeros = built incorrectly (set in makefile) */
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

#ifndef CDB_READ_BUFFER_LENGTH
#define CDB_READ_BUFFER_LENGTH      (256ul)
#endif

#ifndef cdb_assert
#define cdb_assert(X) (assert((X)))
#endif

#define cdb_implies(P, Q)           cdb_assert(!(P) || (Q))

#define CDB_BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))
#define CDB_MIN(X, Y)               ((X) < (Y) ? (X) : (Y))
#define CDB_NBUCKETS                (8ul)
#define CDB_BUCKETS                 (1ul << CDB_NBUCKETS)
#define CDB_FILE_START              (0ul)

/* This enumeration is here and not in the header deliberately, it is to
 * stop error codes becoming part of the API for this library. */
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
	CDB_ERROR_SIZE_E     = -13, /* invalid/unsupported size */
};

typedef struct {
	cdb_word_t position; /* position on disk of this hash table, when known */
	cdb_word_t length;   /* number of buckets in hash table */
} cdb_hash_header_t; /* initial hash table structure */

/* NB. More is allocated than needed for the memory index, it
 * would make things ugly to correct this however, so it will not be. */
typedef struct {
	cdb_word_t *hashes;       /* full key hashes */
	cdb_word_t *fps;          /* file pointers */
	cdb_hash_header_t header; /* header for this hash table */
} cdb_hash_table_t; /* secondary hash table structure */

struct cdb { /* constant database handle: for all your querying needs! */
	cdb_options_t ops;     /* custom file/flash operators */
	void	*file;         /* database handle */
	cdb_word_t file_start, /* start position of structures in file */
	       file_end,       /* end position of database in file, if known, zero otherwise */
	       hash_start;     /* start of secondary hash tables near end of file, if known, zero otherwise */
	cdb_word_t position;   /* read/write/seek position: be careful with this variable! */
	int error;             /* error, if any, any error causes database to be invalid */
	unsigned create : 1,   /* have we opened database up in create mode? */
		 opened : 1,   /* have we successfully opened up the database? */
		 empty  : 1,   /* is the database empty? */
		 sought : 1;   /* have we performed at least one seek (needed to position init cache) */
	cdb_hash_table_t table1[]; /* only allocated if in create mode, BUCKETS elements are allocated */
};

/* To make the library easier to use we could provide a set of default
 * allocators (that when compiled out always return an error), the non-thread
 * safe allocator would return a pointer to a statically declared variable and
 * mark it as being used. */

int cdb_version(unsigned long *version) {
	CDB_BUILD_BUG_ON(sizeof(cdb_word_t) != 2 && sizeof(cdb_word_t) != 4 && sizeof(cdb_word_t) != 8);
	cdb_assert(version);
	unsigned long spec = ((sizeof (cdb_word_t)) * CHAR_BIT) >> 4; /* Lowest three bits = size */
	spec |= CDB_TESTS_ON        << 4;
	spec |= CDB_WRITE_ON        << 5;
	spec |= CDB_MEMORY_INDEX_ON << 6;
	/*spec |= 0                 << 7; */
	*version = (spec << 24) | CDB_VERSION;
	return CDB_VERSION == 0 ? CDB_ERROR_E : CDB_OK_E;
}

int cdb_status(cdb_t *cdb) {
	cdb_assert(cdb);
	return cdb->error;
}

static inline size_t cdb_get_size(cdb_t *cdb) {
	cdb_assert(cdb);
	return cdb->ops.size;
}

static inline uint64_t cdb_get_mask(cdb_t *cdb) {
	cdb_assert(cdb);
	const size_t l = cdb_get_size(cdb);
	if (l == 16/CHAR_BIT)
		return UINT16_MAX;
	if (l == 32/CHAR_BIT)
		return UINT32_MAX;
	cdb_assert(l == 64/CHAR_BIT);
	return UINT64_MAX;
}

/* This is not 'djb2' hash - the character is xor'ed in and not added. This
 * has sometimes been called 'DJB2a'. */
static inline uint32_t cdb_djb_hash(const uint8_t *s, const size_t length) {
	cdb_assert(s);
	uint32_t h = 5381ul;
	for (size_t i = 0; i < length; i++)
		h = ((h << 5ul) + h) ^ s[i]; /* (h * 33) xor c */
	return h;
}

static int cdb_memory_compare(const void *a, const void *b, size_t length) {
	cdb_assert(a);
	cdb_assert(b);
	return memcmp(a, b, length);
}

cdb_word_t cdb_hash(const uint8_t *s, const size_t length) {
	cdb_assert(s);
	return cdb_djb_hash(s, length);
}

static void cdb_preconditions(cdb_t *cdb) {
	cdb_assert(cdb);
	cdb_implies(cdb->file_end   != 0, cdb->file_end   > cdb->file_start);
	cdb_implies(cdb->hash_start != 0, cdb->hash_start > cdb->file_start);
	cdb_assert(cdb->ops.allocator);
	cdb_assert(cdb->ops.read);
	cdb_assert(cdb->ops.open);
	cdb_assert(cdb->ops.close);
	cdb_assert(cdb->ops.seek);
	cdb_assert(cdb->error <= 0);
	cdb_implies(cdb->create, cdb->ops.write);
	/*cdb_assert(cdb->error == 0);*/
}

static inline int cdb_failure(cdb_t *cdb) {
	cdb_preconditions(cdb);
	return cdb->error ? CDB_ERROR_E : CDB_OK_E;
}

static inline int cdb_error(cdb_t *cdb, const int error) {
	cdb_preconditions(cdb);
	if (cdb->error == 0)
		cdb->error = error;
	return cdb_failure(cdb);
}

static inline int cdb_bound_check(cdb_t *cdb, const int fail) {
	cdb_assert(cdb);
	return cdb_error(cdb, fail ? CDB_ERROR_BOUND_E : CDB_OK_E);
}

static inline int cdb_hash_check(cdb_t *cdb, const int fail) {
	cdb_assert(cdb);
	return cdb_error(cdb, fail ? CDB_ERROR_HASH_E : CDB_OK_E);
}

static inline int cdb_overflow_check(cdb_t *cdb, const int fail) {
	cdb_assert(cdb);
	return cdb_error(cdb, fail ? CDB_ERROR_OVERFLOW_E : CDB_OK_E);
}

static inline int cdb_free(cdb_t *cdb, void *p) {
	cdb_assert(cdb);
	if (!p)
		return 0;
	(void)cdb->ops.allocator(cdb->ops.arena, p, 0, 0);
	return 0;
}

static inline void *cdb_allocate(cdb_t *cdb, const size_t length) {
	cdb_preconditions(cdb);
	void *r = cdb->ops.allocator(cdb->ops.arena, NULL, 0, length);
	if (length != 0 && r == NULL)
		(void)cdb_error(cdb, CDB_ERROR_ALLOCATE_E);
	return r ? memset(r, 0, length) : NULL;
}

static inline void *cdb_reallocate(cdb_t *cdb, void *pointer, const size_t length) {
	cdb_preconditions(cdb);
	void *r = cdb->ops.allocator(cdb->ops.arena, pointer, 0, length);
	if (length != 0 && r == NULL)
		(void)cdb_error(cdb, CDB_ERROR_ALLOCATE_E);
	return r;
}

/* NB. A seek can cause buffers to be flushed, which degrades performance quite a lot */
static int cdb_seek_internal(cdb_t *cdb, const cdb_word_t position) {
	cdb_preconditions(cdb);
	if (cdb->error)
		return -1;
	if (cdb->opened && cdb->create == 0)
		if (cdb_bound_check(cdb, position < cdb->file_start || cdb->file_end < position))
			return -1;
	if (cdb->sought == 1u && cdb->position == position)
		return cdb_error(cdb, CDB_OK_E);
	const int r = cdb->ops.seek(cdb->file, position + cdb->ops.offset);
	if (r >= 0) {
		cdb->position = position;
		cdb->sought = 1u;
	}
	return cdb_error(cdb, r < 0 ? CDB_ERROR_SEEK_E : CDB_OK_E);
}

int cdb_seek(cdb_t *cdb, const cdb_word_t position) {
	cdb_preconditions(cdb);
	if (cdb_error(cdb, cdb->create != 0 ? CDB_ERROR_MODE_E : 0))
		return 0;
	return cdb_seek_internal(cdb, position);
}

static cdb_word_t cdb_read_internal(cdb_t *cdb, void *buf, cdb_word_t length) {
	cdb_preconditions(cdb);
	cdb_assert(buf);
	if (cdb_error(cdb, cdb->create != 0 ? CDB_ERROR_MODE_E : 0))
		return 0;
	const cdb_word_t r = cdb->ops.read(cdb->file, buf, length);
	const cdb_word_t n = cdb->position + r;
	if (cdb_overflow_check(cdb, n < cdb->position) < 0)
		return 0;
	cdb->position = n;
	return r;
}

int cdb_read(cdb_t *cdb, void *buf, cdb_word_t length) {
	cdb_preconditions(cdb);
	const cdb_word_t r = cdb_read_internal(cdb, buf, length);
	return cdb_error(cdb, r != length ? CDB_ERROR_READ_E : 0);
}

static cdb_word_t cdb_write(cdb_t *cdb, void *buf, size_t length) {
	cdb_preconditions(cdb);
	cdb_assert(buf);
	if (cdb_error(cdb, cdb->create == 0 ? CDB_ERROR_MODE_E : 0))
		return 0;
	const cdb_word_t r = cdb->ops.write(cdb->file, buf, length);
	const cdb_word_t n = cdb->position + r;
	if (cdb_overflow_check(cdb, n < cdb->position) < 0)
		return 0;
	if (r != length)
		return cdb_error(cdb, CDB_ERROR_WRITE_E);
	cdb->position = n;
	return r;
}

static inline void cdb_pack(uint8_t b[/*static (sizeof (cdb_word_t))*/], cdb_word_t w, size_t l) {
	cdb_assert(b);
	for (size_t i = 0; i < l; i++)
		b[i] = (w >> (i * CHAR_BIT)) & 0xFFu;
}

static inline cdb_word_t cdb_unpack(uint8_t b[/*static (sizeof (cdb_word_t))*/], size_t l) {
	cdb_assert(b);
	cdb_word_t w = 0;
	for (size_t i = 0; i < l; i++)
		w |= ((cdb_word_t)b[i]) << (i * CHAR_BIT);
	return w;
}

int cdb_read_word_pair(cdb_t *cdb, cdb_word_t *w1, cdb_word_t *w2) {
	cdb_assert(cdb);
	cdb_assert(w1);
	cdb_assert(w2);
	const size_t l = cdb_get_size(cdb);
	/* we only need to set this to 'b' to a value to avoid static checkers
	 * signalling a problem, 'b' should be written to be
	 * 'cdb_read_internal' before it is used. */
	uint8_t b[2ul * sizeof(cdb_word_t)] = { 0, };
	const cdb_word_t r = cdb_read_internal(cdb, b, 2ul * l);
	if (r != (cdb_word_t)(2l * l))
		return -1;
	*w1 = cdb_unpack(b, l);
	*w2 = cdb_unpack(b + l, l);
	return 0;
}

static int cdb_write_word_pair(cdb_t *cdb, const cdb_word_t w1, const cdb_word_t w2) {
	cdb_assert(cdb);
	const size_t l = cdb_get_size(cdb);
	uint8_t b[2ul * sizeof(cdb_word_t)]; /* NOT INITIALIZED */
	cdb_pack(b,     w1, l);
	cdb_pack(b + l, w2, l);
	if (cdb_write(cdb, b, 2ul * l) != (2ul * l))
		return -1;
	return 0;
}

static int cdb_hash_free(cdb_t *cdb, cdb_hash_table_t *t) {
	cdb_assert(cdb);
	cdb_assert(t);
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
	for (size_t i = 0; cdb->create && i < CDB_BUCKETS; i++)
		if (cdb_hash_free(cdb, &cdb->table1[i]) < 0)
			r = -1;
	(void)cdb_error(cdb, CDB_ERROR_E);
	(void)cdb->ops.allocator(cdb->ops.arena, cdb, 0, 0);
	return r;
}

static inline int cdb_finalize(cdb_t *cdb) { /* write hash tables to disk */
	cdb_assert(cdb);
	cdb_assert(cdb->error == 0);
	cdb_assert(cdb->create == 1);
	if (CDB_WRITE_ON == 0)
		return cdb_error(cdb, CDB_ERROR_DISABLED_E);
	int r = 0;
	cdb_word_t mlen = 8;
	cdb_word_t *hashes    = cdb_allocate(cdb, mlen * sizeof *hashes);
	cdb_word_t *positions = cdb_allocate(cdb, mlen * sizeof *positions);
	if (!hashes || !positions)
		goto fail;
	/* NB. No need to seek as we are the only thing that can affect
	 * cdb->position in write mode */
	cdb->hash_start = cdb->position;

	for (size_t i = 0; i < CDB_BUCKETS; i++) { /* write tables at end of file */
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
			const cdb_word_t start = (h >> CDB_NBUCKETS) % length;
			cdb_word_t k = 0;
			for (k = start; positions[k]; k = (k + 1ul) % length)
				;
			hashes[k]    = h;
			positions[k] = p;
		}

		for (cdb_word_t j = 0; j < length; j++)
			if (cdb_write_word_pair(cdb, hashes[j], positions[j]) < 0)
				goto fail;
	}
	cdb->file_end = cdb->position;
	if (cdb_seek_internal(cdb, cdb->file_start) < 0)
		goto fail;
	for (size_t i = 0; i < CDB_BUCKETS; i++) { /* write initial hash table */
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
int cdb_open(cdb_t **cdb, const cdb_options_t *ops, const int create, const char *file) {
	/* We could allow the word size of the CDB database {16, 32 (default) or 64}
	 * to be configured at run time and not compile time, this has API related
	 * consequences, the size of 'cdb_word_t' would determine maximum size that
	 * could be supported by this library. 'cdb_open' would have to take another
	 * parameter or one of the structures passed in would need to be extended. */
	cdb_assert(cdb);
	cdb_assert(ops);
	cdb_assert(ops->allocator);
	cdb_assert(ops->read);
	cdb_assert(ops->open);
	cdb_assert(ops->close);
	cdb_assert(ops->seek);
	cdb_implies(create, ops->write);
	CDB_BUILD_BUG_ON(CHAR_BIT != 8);
	/* ops->flush is optional */
	*cdb = NULL;
	if (create && CDB_WRITE_ON == 0)
		return CDB_ERROR_E;
	if (ops->size != 0 && ops->size != 16 && ops->size != 32 && ops->size != 64)
		return CDB_ERROR_SIZE_E;
	if (ops->size != 0 && ops->size > (sizeof(cdb_word_t) * CHAR_BIT))
		return CDB_ERROR_SIZE_E;
	cdb_t *c = NULL;
	const int large = CDB_MEMORY_INDEX_ON || create;
	const size_t csz = (sizeof *c) + (large * sizeof c->table1[0] * CDB_BUCKETS);
	c = ops->allocator(ops->arena, NULL, 0, csz);
	if (!c)
		goto fail;
	memset(c, 0, csz);
	c->ops         = *ops;
	c->ops.size    = c->ops.size    ? c->ops.size / CHAR_BIT : (32ul / CHAR_BIT);
	c->ops.hash    = c->ops.hash    ? c->ops.hash    : cdb_hash;
	c->ops.compare = c->ops.compare ? c->ops.compare : cdb_memory_compare;
	c->create      = create;
	c->empty       = 1;
	*cdb           = c;
	c->file_start  = CDB_FILE_START;
	c->file        = c->ops.open(file, create ? CDB_RW_MODE : CDB_RO_MODE);
	if (!(c->file)) {
		(void)cdb_error(c, CDB_ERROR_OPEN_E);
		goto fail;
	}
	if (cdb_seek_internal(c, c->file_start) < 0)
		goto fail;
	if (create) {
		for (size_t i = 0; i < CDB_BUCKETS; i++) /* write empty header */
			if (cdb_write_word_pair(c, 0, 0) < 0)
				goto fail;
	} else {
		/* We allocate more memory than we need if CDB_MEMORY_INDEX_ON is
		 * true as 'cdb_hash_table_t' contains entries needed for
		 * creation that we do not need when reading the database. */
		cdb_word_t hpos = 0, hlen = 0, lpos = -1l, lset = 0, prev = 0, pnum = 0;
		for (size_t i = 0; i < CDB_BUCKETS; i++) {
			cdb_hash_table_t t = { .header = { .position = 0, .length = 0 } };
			if (cdb_read_word_pair(c, &t.header.position, &t.header.length) < 0)
				goto fail;
			if (i && t.header.position != (prev + (pnum * (2ul * cdb_get_size(c)))))
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
		if (cdb_seek_internal(c, c->file_start) < 0)
			goto fail;
		c->file_end   = hpos + (hlen * (2ul * cdb_get_size(c)));
		c->hash_start = lset ? lpos : (CDB_BUCKETS * (2ul * cdb_get_size(c)));
		if (lset) {
			if (cdb_bound_check(c, c->file_start > lpos) < 0)
				goto fail;
		}
		if (cdb_overflow_check(c, c->file_end < hpos) < 0)
			goto fail;
	}
	c->opened = 1;
	return CDB_OK_E;
fail:
	(void)cdb_close(c);
	return CDB_ERROR_E;
}

/* returns: -1 = error, 0 = not equal, 1 = equal */
static int cdb_compare(cdb_t *cdb, const cdb_buffer_t *k1, const cdb_file_pos_t *k2) {
	cdb_assert(cdb);
	cdb_assert(cdb->ops.compare);
	cdb_assert(k1);
	cdb_assert(k2);
	if (k1->length != k2->length)
		return CDB_NOT_FOUND_E; /* not equal */
	const cdb_word_t length = k1->length;
	if (cdb_seek_internal(cdb, k2->position) < 0)
		return CDB_ERROR_E;
	for (cdb_word_t i = 0; i < length; i += CDB_READ_BUFFER_LENGTH) {
		/* Note that making this buffer larger may not make things faster - if
		 * most keys differ in the first few bytes then a smaller buffer means
		 * less bytes moved around before the comparison. */
		uint8_t kbuf[CDB_READ_BUFFER_LENGTH];
		CDB_BUILD_BUG_ON(sizeof kbuf != CDB_READ_BUFFER_LENGTH);
		const cdb_word_t rl = CDB_MIN((cdb_word_t)sizeof kbuf, (cdb_word_t)length - i);
		if (cdb_read_internal(cdb, kbuf, rl) != rl)
			return CDB_ERROR_E;
		if (cdb->ops.compare(k1->buffer + i, kbuf, rl))
			return CDB_NOT_FOUND_E;
	}
	return CDB_FOUND_E; /* equal */
}

static int cdb_retrieve(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value, uint64_t *record) {
	cdb_assert(cdb);
	cdb_assert(cdb->opened);
	cdb_assert(cdb->ops.hash);
	cdb_assert(key); /* If key was NULL, we *could* lookup the values instead using cdb_foreach */
	cdb_assert(value);
	cdb_assert(record);
	cdb_word_t pos = 0, num = 0, h = 0;
	uint64_t wanted = *record, recno = 0;
	*record = 0;
	*value = (cdb_file_pos_t) { 0, 0, };
	if (cdb->error)
		goto fail;
	if (cdb->create) {
		(void)cdb_error(cdb, CDB_ERROR_MODE_E);
		goto fail;
	}
	/* It is usually a good idea to include the length as part of the data
	 * of the hash, however that would make the format incompatible. */
	h = cdb->ops.hash((uint8_t *)(key->buffer), key->length) & cdb_get_mask(cdb); /* locate key in first table */
	if (CDB_MEMORY_INDEX_ON) { /* use more memory (~4KiB) to speed up first match */
		cdb_hash_table_t *t = &cdb->table1[h % CDB_BUCKETS];
		pos = t->header.position;
		num = t->header.length;
	} else {
		if (cdb_seek_internal(cdb, cdb->file_start + ((h % CDB_BUCKETS) * (2ul * cdb_get_size(cdb)))) < 0)
			goto fail;
		if (cdb_read_word_pair(cdb, &pos, &num) < 0)
			goto fail;
	}
	if (num == 0) /* no keys in this bucket -> key not found */
		return cdb_failure(cdb) < 0 ? CDB_ERROR_E : CDB_NOT_FOUND_E;
	if (cdb_bound_check(cdb, pos > cdb->file_end || pos < cdb->hash_start) < 0)
		goto fail;
	const cdb_word_t start = (h >> CDB_NBUCKETS) % num;
	for (cdb_word_t i = 0; i < num; i++) {
		const cdb_word_t seekpos = pos + (((start + i) % num) * (2ul * cdb_get_size(cdb)));
		if (seekpos < pos || seekpos > cdb->file_end)
			goto fail;
		if (cdb_seek_internal(cdb, seekpos) < 0)
			goto fail;
		cdb_word_t h1 = 0, p1 = 0;
		if (cdb_read_word_pair(cdb, &h1, &p1) < 0)
			goto fail;
		if (cdb_bound_check(cdb, p1 > cdb->hash_start) < 0) /* key-value pair should not overlap with hash tables section */
			goto fail;
		if (p1 == 0) { /* end of list */
			*record         = recno;
			return cdb_failure(cdb) < 0 ? CDB_ERROR_E : CDB_NOT_FOUND_E;
		}
		if (cdb_hash_check(cdb, (h1 & 0xFFul) != (h & 0xFFul)) < 0) /* buckets bits should be the same */
			goto fail;
		if (h1 == h) { /* possible match */
			if (cdb_seek_internal(cdb, p1) < 0)
				goto fail;
			cdb_word_t klen = 0, vlen = 0;
			if (cdb_read_word_pair(cdb, &klen, &vlen) < 0)
				goto fail;
			const cdb_file_pos_t k2 = { .length = klen, .position = p1 + (2ul * cdb_get_size(cdb)) };
			if (cdb_overflow_check(cdb, k2.position < p1 || (k2.position + klen) < k2.position) < 0)
				goto fail;
			if (cdb_bound_check(cdb, k2.position + klen > cdb->hash_start) < 0)
				goto fail;
			const int comp = cdb_compare(cdb, key, &k2);
			const int found = comp > 0;
			if (comp < 0)
				goto fail;
			if (found && recno == wanted) { /* found key, correct record? */
				cdb_file_pos_t v2 = { .length = vlen, .position = k2.position + klen };
				if (cdb_overflow_check(cdb, (v2.position + v2.length) < v2.position) < 0)
					goto fail;
				if (cdb_bound_check(cdb, v2.position > cdb->hash_start) < 0)
					goto fail;
				if (cdb_bound_check(cdb, (v2.position + v2.length) > cdb->hash_start) < 0)
					goto fail;
				*value          = v2;
				*record         = recno;
				return cdb_failure(cdb) < 0 ? CDB_ERROR_E : CDB_FOUND_E;
			}
			recno += found;
		}
	}
	*record         = recno;
	return cdb_failure(cdb) < 0 ? CDB_ERROR_E : CDB_NOT_FOUND_E;
fail:
	return cdb_error(cdb, CDB_ERROR_E);
}

int cdb_lookup(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value, uint64_t record) {
	cdb_assert(cdb);
	cdb_assert(cdb->opened);
	cdb_assert(key);
	cdb_assert(value);
	return cdb_retrieve(cdb, key, value, &record);
}

int cdb_get(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value) {
	cdb_assert(cdb);
	cdb_assert(cdb->opened);
	cdb_assert(key);
	cdb_assert(value);
	return cdb_lookup(cdb, key, value, 0l);
}

int cdb_count(cdb_t *cdb, const cdb_buffer_t *key, uint64_t *count) {
	cdb_assert(cdb);
	cdb_assert(cdb->opened);
	cdb_assert(key);
	cdb_assert(count);
	cdb_file_pos_t value = { 0, 0, };
	uint64_t c = UINT64_MAX;
	const int r = cdb_retrieve(cdb, key, &value, &c);
	c = r == CDB_FOUND_E ? c + 1l : c;
	*count = c;
	return r;
}

int cdb_foreach(cdb_t *cdb, cdb_callback cb, void *param) {
	cdb_assert(cdb);
	cdb_assert(cdb->opened);
	if (cdb->error || cdb->create)
		goto fail;
	cdb_word_t pos = cdb->file_start + (256ul * (2ul * cdb_get_size(cdb)));
	int r = 0;
	for (;pos < cdb->hash_start;) {
		if (cdb_seek_internal(cdb, pos) < 0)
			goto fail;
		cdb_word_t klen = 0, vlen = 0;
		if (cdb_read_word_pair(cdb, &klen, &vlen) < 0)
			goto fail;
		const cdb_file_pos_t key   = { .length = klen, .position = pos + (2ul * cdb_get_size(cdb)), };
		const cdb_file_pos_t value = { .length = vlen, .position = pos + (2ul * cdb_get_size(cdb)) + klen, };
		if (cdb_bound_check(cdb, value.position > cdb->hash_start) < 0)
			goto fail;
		if (cdb_bound_check(cdb, (value.position + value.length) > cdb->hash_start) < 0)
			goto fail;
		r = cb ? cb(cdb, &key, &value, param) : 0;
		if (r < 0)
			goto fail;
		if (r > 0) /* early termination */
			break;
		pos = value.position + value.length;
	}
	return cdb_failure(cdb) < 0 ? CDB_ERROR_E : r;
fail:
	return cdb_error(cdb, CDB_ERROR_E);
}

static int cdb_round_up_to_next_power_of_two(const cdb_word_t x) {
	cdb_word_t p = 1ul;
	while (p < x)
		p <<= 1ul;
	return p;
}

static int cdb_hash_grow(cdb_t *cdb, const cdb_word_t hash, const cdb_word_t position) {
	cdb_assert(cdb);
	cdb_hash_table_t *t1 = &cdb->table1[hash % CDB_BUCKETS];
	cdb_word_t *hashes = t1->hashes, *fps = t1->fps;
	const cdb_word_t next = cdb_round_up_to_next_power_of_two(t1->header.length + 1ul);
	const cdb_word_t cur  = cdb_round_up_to_next_power_of_two(t1->header.length);
	if (cdb_overflow_check(cdb, (t1->header.length + 1ul) < t1->header.length) < 0)
		return CDB_ERROR_E;
	if (next > cur || t1->header.length == 0) {
		const cdb_word_t alloc = t1->header.length == 0 ? 1ul : t1->header.length * 2ul;
		if (cdb_overflow_check(cdb, (t1->header.length * 2ul) < t1->header.length) < 0)
			return CDB_ERROR_E;
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
	return cdb_failure(cdb);
}

/* Duplicate keys can be added. To prevent this the library could easily be
 * improved in a backwards compatible way by extending the options structure
 * to include a new options value that would specify if adding duplicate keys
 * is allowed (adding values to the end of a structure being backwards
 * compatible in (most/all?) C ABIs). "cdb_add" would then need to be extended
 * to check for duplicate keys, which would be the difficult bit, a new lookup
 * function would need to be designed that could query the partially written
 * database. */
int cdb_add(cdb_t *cdb, const cdb_buffer_t *key, const cdb_buffer_t *value) {
	cdb_preconditions(cdb);
	cdb_assert(cdb->opened);
	cdb_assert(cdb->ops.hash);
	cdb_assert(key);
	cdb_assert(value);
	cdb_assert(cdb->position >= cdb->file_start);
	if (CDB_WRITE_ON == 0)
		return cdb_error(cdb, CDB_ERROR_DISABLED_E);
	if (cdb->error)
		goto fail;
	if (cdb->create == 0) {
		(void)cdb_error(cdb, CDB_ERROR_MODE_E);
		goto fail;
	}
	if (cdb_overflow_check(cdb, (key->length + value->length) < key->length) < 0)
		goto fail;
	const cdb_word_t h = cdb->ops.hash((uint8_t*)(key->buffer), key->length) & cdb_get_mask(cdb);
		if (cdb_hash_grow(cdb, h, cdb->position) < 0)
		goto fail;
	if (cdb_seek_internal(cdb, cdb->position) < 0)
		goto fail;
	if (cdb_write_word_pair(cdb, key->length, value->length) < 0)
		goto fail;
	if (cdb_write(cdb, key->buffer, key->length) != key->length)
		goto fail;
	if (cdb_write(cdb, value->buffer, value->length) != value->length)
		goto fail;
	cdb->empty = 0;
	return cdb_failure(cdb);
fail:
	return cdb_error(cdb, CDB_ERROR_E);
}

uint64_t cdb_prng(uint64_t s[2]) { /* XORSHIFT128: A few rounds of SPECK or TEA ciphers also make good PRNG */
	cdb_assert(s);
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

#define CDB_TEST_VECTOR_LEN (1024ul)

/* A series of optional unit tests that can be compiled out
 * of the program, the function will still remain even if the
 * contents of it are elided. */
int cdb_tests(const cdb_options_t *ops, const char *test_file) {
	cdb_assert(ops);
	cdb_assert(test_file);
	CDB_BUILD_BUG_ON(sizeof (cdb_word_t) < 2);

	/* See readme.md for description of this and why this
	 * is the way it is. Note that if "CDB_TESTS_ON" is
	 * zero the rest of the code will be removed by the
	 * compiler though. */
	if (CDB_TESTS_ON == 0) 
		return CDB_OK_E;

	const size_t l = ops->size;
	const size_t vectors = l == 16ul ? 128ul : CDB_TEST_VECTOR_LEN;
	const size_t klen    = l == 16ul ?  64ul : CDB_TEST_VECTOR_LEN;
	const size_t vlen    = l == 16ul ?  64ul : CDB_TEST_VECTOR_LEN;

	typedef struct {
		char key[CDB_TEST_VECTOR_LEN], value[CDB_TEST_VECTOR_LEN], result[CDB_TEST_VECTOR_LEN];
		uint64_t recno;
		cdb_word_t klen, vlen;
	} test_t;

	typedef struct { char *key, *value; } test_duplicate_t;

	static const test_duplicate_t dups[] = { /* add known duplicates */
		{ "ALPHA",    "BRAVO",     },
		{ "ALPHA",    "CHARLIE",   },
		{ "ALPHA",    "DELTA",     },
		{ "FSF",      "Collide-1", },
		{ "Aug",      "Collide-2", },
		{ "FSF",      "Collide-3", },
		{ "Aug",      "Collide-4", },
		{ "revolves", "Collide-1", },
		{ "revolt's", "Collide-2", },
		{ "revolt's", "Collide-3", },
		{ "revolt's", "Collide-4", },
		{ "revolves", "Collide-5", },
		{ "revolves", "Collide-6", },
		{ "1234",     "5678",      },
		{ "1234",     "9ABC",      },
		{ "",         "",          },
		{ "",         "X",         },
		{ "",         "",          },
	};
	const size_t dupcnt = sizeof (dups) / sizeof (dups[0]);

	cdb_t *cdb = NULL;
	test_t *ts = NULL;
	uint64_t s[2] = { 0, };
	int r = CDB_OK_E;

	if (cdb_open(&cdb, ops, 1, test_file) < 0)
		return CDB_ERROR_E;

	if (!(ts = cdb_allocate(cdb, (dupcnt + vectors) * (sizeof *ts))))
		goto fail;

	for (unsigned i = 0; i < vectors; i++) {
		char *k = ts[i].key;
		char *v = ts[i].value;
		const cdb_word_t kl = (cdb_prng(s) % (klen - 1ul)) + 1ul;
		const cdb_word_t vl = (cdb_prng(s) % (vlen - 1ul)) + 1ul;
		for (unsigned long j = 0; j < kl; j++)
			k[j] = 'a' + (cdb_prng(s) % 26);
		for (unsigned long j = 0; j < vl; j++)
			v[j] = 'a' + (cdb_prng(s) % 26);
		const cdb_buffer_t key   = { .length = kl, .buffer = k };
	       	const cdb_buffer_t value = { .length = vl, .buffer = v };
		for (unsigned long j = 0; j < i; j++)
			if (memcmp(ts[i].value, ts[j].value, vlen) == 0)
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

		memcpy(ts[i + vectors].key,   key.buffer,   key.length);
		memcpy(ts[i + vectors].value, value.buffer, value.length);

		for (unsigned long j = 0; j < i; j++)
			if (memcmp(ts[i].value, ts[j].value, vlen) == 0)
				ts[i].recno++;

		if (cdb_add(cdb, &key, &value) < 0)
			goto fail;
	}


	if (cdb_close(cdb) < 0) {
		(void)ops->allocator(ops->arena, ts, 0, 0);
		return -1;
	}
	cdb = NULL;

	if (cdb_open(&cdb, ops, 0, test_file) < 0) {
		(void)ops->allocator(ops->arena, ts, 0, 0);
		return -1;
	}

	for (unsigned i = 0; i < (vectors + dupcnt); i++) {
		test_t *t = &ts[i];
		const cdb_buffer_t key = { .length = t->klen, .buffer = t->key };
		cdb_file_pos_t result = { 0, 0 }, discard = { 0, 0 };
		const int g = cdb_lookup(cdb, &key, &result, t->recno);
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

		if (result.length > vlen)
			goto fail;
		if (result.length != t->vlen) {
			r = -5;
		} else {
			if (cdb_seek_internal(cdb, result.position) < 0)
				goto fail;
			if (cdb_read_internal(cdb, t->result, result.length) != result.length)
				goto fail;
			if (memcmp(t->result, t->value, result.length))
				r = -6;
		}

		uint64_t cnt = 0;
		if (cdb_count(cdb, &key, &cnt) < 0)
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
	(void)ops->allocator(ops->arena, ts, 0, 0);
	(void)cdb_close(cdb);
	return CDB_ERROR_E;
}

