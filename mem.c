#include "cdb.h"
#include "mem.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

#define UNUSED(X) ((void)(X))

#ifndef MIN
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#endif

static void mem_asserts(cdb_mem_t *m) {
	assert(m);
	assert(m->cdb);
	assert(m->m);
	assert(m->pos <= m->length);
	assert(m->mode == CDB_RO_MODE || m->mode == CDB_RW_MODE);
	assert(m->uniq_ptr == &cdb_mem_options); /* !! */
}

static void *cdb_allocator_cb(void *arena, void *ptr, const size_t oldsz, const size_t newsz) {
	UNUSED(arena);
	if (newsz == 0) {
		free(ptr);
		return NULL;
	}
	if (newsz > oldsz)
		return realloc(ptr, newsz);
	return ptr;
}

int cdb_mem(cdb_t *cdb, cdb_mem_t **mem) {
	assert(cdb);
	*mem = NULL;
	cdb_mem_t *m = cdb_get_handle(cdb);
	if (m->uniq_ptr != &cdb_mem_options)
		return -1;
	mem_asserts(m);
	*mem = m;
	return 0;
}

static int grow(cdb_t *cdb, cdb_mem_t *m) {
	assert(cdb);
	assert(m);
	const size_t newsz = m->length ? 4096 : m->length * 2;
	if (newsz < m->length)
		return -1;
	void *r = cdb_reallocate(cdb, m->m, newsz);
	if (!r)
		return -1;
	m->m = r;
	m->length = newsz;
	return 0;
}

static cdb_word_t cdb_read_cb(void *file, void *buf, size_t length) {
	assert(file);
	assert(buf);
	cdb_mem_t *m = file;
	mem_asserts(m);
	const size_t remaining = m->length - m->pos;
	assert(remaining <= m->length);
	const size_t rd = MIN(remaining, length);
	memcpy(buf, &m->m[m->pos], rd);
	m->pos += rd;
	return 0;
}

static cdb_word_t cdb_write_cb(void *file, void *buf, size_t length) {
	assert(file);
	assert(buf);
	cdb_mem_t *m = file;
	mem_asserts(m);
	const size_t remaining = m->length - m->pos;
	assert(remaining <= m->length);
	if (length > remaining) {
		if (grow(m->cdb, m) < 0) {
			return 0; /* signifies error */
		}
	}
	memcpy(&m->m[m->pos], buf, length);
	m->pos += length;
	return length;
}

static int cdb_seek_cb(void *file, uint64_t offset) {
	assert(file);
	cdb_mem_t *m = file;
	mem_asserts(m);
	const size_t l = MIN(offset, m->length);
	m->pos = l;
	return 0;
}

/* TODO: Find best way to set `*m` in read mode? */
static void *cdb_open_cb(cdb_t *cdb, const char *name, int mode) {
	assert(cdb);
	assert(name);
	assert(mode == CDB_RO_MODE || mode == CDB_RW_MODE);
	cdb_mem_t *m = cdb_allocate(cdb, sizeof (*m));
	if (!m)
		return NULL;
	m->cdb = cdb;
	m->uniq_ptr = &cdb_mem_options; /* unique pointer; set for sanity checking */
	return m;
}

static int cdb_close_cb(cdb_t *cdb, void *file) {
	assert(cdb);
	assert(file);
	cdb_mem_t *m = file;
	mem_asserts(m);
	const int r = cdb_free(cdb, m->m);
	m->m = NULL;
	if (cdb_free(cdb, m) < 0)
		return -1;
	return r;
}

static int cdb_flush_cb(void *file) {
	assert(file);
	cdb_mem_t *m = file;
	if (m->mode == CDB_RO_MODE)
		return -1;
	return 0;
}

const cdb_callbacks_t cdb_mem_options = {
	.allocator = cdb_allocator_cb,
	.hash      = NULL,
	.compare   = NULL,
	.read      = cdb_read_cb,
	.write     = cdb_write_cb,
	.seek      = cdb_seek_cb,
	.open      = cdb_open_cb,
	.close     = cdb_close_cb,
	.flush     = cdb_flush_cb,
	.arena     = NULL,
	.offset    = 0,
	.size      = 0, /* auto-select */
};

