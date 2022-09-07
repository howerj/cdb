#include "cdb.h"
#include "host.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define UNUSED(X) ((void)(X))

typedef struct {
	FILE *handle;
	size_t length;
	char buffer[];
} file_t;

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

static cdb_word_t cdb_read_cb(void *file, void *buf, size_t length) {
	assert(file);
	assert(buf);
	assert(((file_t*)file)->handle);
	return fread(buf, 1, length, ((file_t*)file)->handle);
}

static cdb_word_t cdb_write_cb(void *file, void *buf, size_t length) {
	assert(file);
	assert(buf);
	assert(((file_t*)file)->handle);
	return fwrite(buf, 1, length, ((file_t*)file)->handle);
}

static int cdb_seek_cb(void *file, uint64_t offset) {
	assert(file);
	assert(((file_t*)file)->handle);
	return fseek(((file_t*)file)->handle, offset, SEEK_SET);
}

static void *cdb_open_cb(const char *name, int mode) {
	assert(name);
	assert(mode == CDB_RO_MODE || mode == CDB_RW_MODE);
	const char *mode_string = mode == CDB_RW_MODE ? "wb+" : "rb";
	FILE *f = fopen(name, mode_string);
	if (!f)
		return f;
	const size_t length = 1024ul * 16ul;
	file_t *fb = malloc(sizeof (*f) + length);
	if (!fb) {
		fclose(f);
		return NULL;
	}
	fb->handle = f;
	fb->length = length;
	if (setvbuf(f, fb->buffer, _IOFBF, fb->length) < 0) {
		fclose(f);
		free(fb);
		return NULL;
	}
	return fb;
}

static int cdb_close_cb(void *file) {
	assert(file);
	assert(((file_t*)file)->handle);
	const int r = fclose(((file_t*)file)->handle);
	((file_t*)file)->handle = NULL;
	free(file);
	return r;
}

static int cdb_flush_cb(void *file) {
	assert(file);
	return fflush(((file_t*)file)->handle);
}

const cdb_options_t cdb_host_options = {
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

