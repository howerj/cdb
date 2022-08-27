/* Program: Constant Database C API
 * Author:  Richard James Howe
 * Email:   howe.r.j.89@gmail.com
 * License: Unlicense
 * Repo:    <https://github.com/howerj/cdb> */
#ifndef CDB_H
#define CDB_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#ifndef CDB_API
#define CDB_API /* Used to apply attributes to exported functions */
#endif

#ifndef CDB_WORD_T
typedef uint64_t cdb_word_t; /* valid sizes: uint64_t, uint32_t, uint16_t */
#endif

#ifndef ALLOCATOR_FN
#define ALLOCATOR_FN
typedef void *(*allocator_fn)(void *arena, void *ptr, size_t oldsz, size_t newsz);
#endif

struct cdb;
typedef struct cdb cdb_t;

enum { CDB_RO_MODE, CDB_RW_MODE };

typedef struct {
	allocator_fn allocator;
	cdb_word_t (*hash)(const uint8_t *data, size_t length); /* hash function: NULL defaults to djb hash */
	int (*compare)(const void *a, const void *b, size_t length); /* key comparison function: NULL defaults to memcmp */
	cdb_word_t (*read)(void *file, void *buf, size_t length);
	cdb_word_t (*write)(void *file, void *buf, size_t length); /* (conditionally optional) needed for db creation only */
	int (*seek)(void *file, long offset);
	void *(*open)(const char *name, int mode);
	int (*close)(void *file);
	int (*flush)(void *file); /* (optional) called at end of successful creation */

	void *arena;       /* used for 'arena' argument for the allocator, can be NULL if allocator allows it */
	cdb_word_t offset; /* starting offset for CDB file if not at beginning of file */
	unsigned size;     /* Either 0 (same as 32), 16, 32 or 64, but cannot be bigger than 'sizeof(cdb_word_t)*8' */
} cdb_options_t; /* a file abstraction layer, could point to memory, flash, or disk */

typedef struct {
	cdb_word_t length; /* length of data */
	char *buffer;      /* pointer to arbitrary data */
} cdb_buffer_t; /* used to represent a key or value in memory */

typedef struct {
	cdb_word_t position; /* position in file, for use with cdb_read/cdb_seek */
	cdb_word_t length;   /* length of data on disk, for use with cdb_read */
} cdb_file_pos_t; /* used to represent a value on disk that can be accessed via 'cdb_options_t' */

typedef int (*cdb_callback)(cdb_t *cdb, const cdb_file_pos_t *key, const cdb_file_pos_t *value, void *param);

/* All functions return: < 0 on failure, 0 on success/not found, 1 on found if applicable */
CDB_API int cdb_open(cdb_t **cdb, const cdb_options_t *ops, int create, const char *file); /* arena may be NULL */
CDB_API int cdb_close(cdb_t *cdb);  /* free cdb, close (and write to disk if in create mode) */
CDB_API int cdb_read(cdb_t *cdb, void *buf, cdb_word_t length); /* returns error code not length! */
CDB_API int cdb_add(cdb_t *cdb, const cdb_buffer_t *key, const cdb_buffer_t *value); /* do not call cdb_read and/or cdb_seek in open mode */
CDB_API int cdb_seek(cdb_t *cdb, cdb_word_t position);
CDB_API int cdb_foreach(cdb_t *cdb, cdb_callback cb, void *param);
CDB_API int cdb_read_word_pair(cdb_t *cdb, cdb_word_t *w1, cdb_word_t *w2);
CDB_API int cdb_get(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value);
CDB_API int cdb_lookup(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value, long record);
CDB_API int cdb_count(cdb_t *cdb, const cdb_buffer_t *key, long *count);
CDB_API int cdb_status(cdb_t *cdb);
CDB_API int cdb_version(unsigned long *version); /* version number in x.y.z format, z = LSB, MSB is library info */
CDB_API int cdb_tests(const cdb_options_t *ops, const char *test_file);

#ifdef __cplusplus
}
#endif
#endif
