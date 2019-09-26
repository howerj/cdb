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

#ifndef CDB_API
#define CDB_API /* Used to apply attributes to exported functions */
#endif

struct cdb;
typedef struct cdb cdb_t;

enum { CDB_SEEK_START, CDB_SEEK_CURRENT, CDB_SEEK_END, };
enum { CDB_RO_MODE, CDB_RW_MODE };

typedef struct {
	void *(*malloc)(void *arena, size_t length);
	void *(*realloc)(void *arena, void *pointer, size_t length);
	int (*free)(void *arena, void *pointer);
	void *arena;
} cdb_allocator_t; /* custom allocator interface; mostly used for creation of data-base */

typedef struct {
	long (*read)(void *file, void *buf, size_t length);
	long (*write)(void *file, void *buf, size_t length); /* needed for creation only */
	long (*seek)(void *file, long offset, long whence);
	void *(*open)(const char *name, int mode);
	long (*close)(void *file);
	long (*flush)(void *file); /* called at end of successful creation */
} cdb_file_operators_t; /* a file abstraction layer, could point to memory, flash, or disk */

typedef struct {
	size_t length;
	char *buffer;
} cdb_buffer_t; /* used to represent a key or value in memory */

typedef struct {
	unsigned long position;
	unsigned long length;
} cdb_file_pos_t; /* used to represent a value on disk that can be accessed via 'cdb_file_operators_t' */

typedef int (*cdb_callback)(cdb_t *cdb, const cdb_file_pos_t *key, const cdb_file_pos_t *value, void *param);

/* All functions return: -1 on failure, 0 on success */
CDB_API int cdb_open(cdb_t **cdb, cdb_file_operators_t *ops, cdb_allocator_t *allocator, int create, const char *file);
CDB_API int cdb_close(cdb_t *cdb);  /* free cdb, close (and write to disk if in create mode) */
CDB_API int cdb_get(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value); /* returns: -1 on error, 0 on not found, 1 on found */
CDB_API int cdb_get_record(cdb_t *cdb, const cdb_buffer_t *key, int record, cdb_file_pos_t *value); /* returns: -1 on error, 0 on not found, 1 on found */
CDB_API int cdb_foreach(cdb_t *cdb, cdb_callback cb, void *param);
CDB_API int cdb_add(cdb_t *cdb, const cdb_buffer_t *key, const cdb_buffer_t *value);
CDB_API int cdb_tests(cdb_file_operators_t *ops, cdb_allocator_t *allocator, const char *test_file); /* returns 0 on success (or NDEBUG defined), -1 on failure */

CDB_API void *cdb_get_file(cdb_t *cdb);

#ifdef __cplusplus
}
#endif
#endif
