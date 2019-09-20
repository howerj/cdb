#include "cdb.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define UNUSED(X) ((void)(X))

static long cdb_read_cb(void *file, void *buf, size_t length) {
	assert(file);
	assert(buf);
	return fread(buf, 1, length, (FILE*)file);
}

static long cdb_write_cb(void *file, void *buf, size_t length) {
	assert(file);
	assert(buf);
	return fwrite(buf, 1, length, (FILE*)file);
}

static long cdb_seek_cb(void *file, long offset, long whence) {
	assert(file);
	return 0;
}

void *cdb_open_cb(const char *name, const char *mode) {
	assert(name);
	assert(mode);
	return fopen(name, mode);
}

long cdb_close_cb(void *file) {
	assert(file);
	return fclose((FILE*)file);
}

static cdb_file_operators_t ops = {
	.read = cdb_read_cb,
	.write = cdb_write_cb,
	.seek = cdb_seek_cb,
	.open = cdb_open_cb,
	.close = cdb_close_cb,
	.file = NULL,
};

static void *cdb_malloc_cb(void *arena, const size_t length) {
	UNUSED(arena);
	return malloc(length);
}

static void *cdb_realloc_cb(void *arena, void *pointer, const size_t length) {
	UNUSED(arena);
	return realloc(pointer, length);
}

static int cdb_free_cb(void *arena, void *pointer) {
	UNUSED(arena);
	free(pointer);
	return 0;
}
static cdb_allocator_t allocator = {
	.malloc = cdb_malloc_cb,
	.realloc = cdb_realloc_cb,
	.free = cdb_free_cb,
	.arena = NULL,
};

int main(int argc, char **argv) {
	return 0;
}
