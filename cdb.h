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

typedef struct {
	void *(*malloc)(void *arena, size_t length);
	void *(*realloc)(void *arena, void *pointer, size_t length);
	int (*free)(void *arena, void *pointer);
	void *arena;
} cdb_allocator_t;

typedef struct {
	long (*read)(void *file, void *buf, size_t length);
	long (*write)(void *file, void *buf, size_t length); /* needed for creation only */
	long (*seek)(void *file, long offset, long whence);
	void *(*open)(const char *name, const char *mode);
	long (*close)(void *file);
	void *file;
} cdb_file_operators_t;

typedef struct {
	size_t length;
	char *buffer;
} cdb_buffer_t;

typedef struct {
	unsigned long position;
	unsigned long length;
} cdb_file_pos_t;

typedef int (*cdb_callback)(cdb_t *cdb, const cdb_file_pos_t *key, const cdb_file_pos_t *value, void *param);

/* All functions return: -1 on failure, 0 on success */

CDB_API int cdb_open(cdb_t **cdb, cdb_file_operators_t *ops, cdb_allocator_t *allocator, int create);
CDB_API int cdb_close(cdb_t *cdb);
CDB_API int cdb_get(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value);
CDB_API int cdb_foreach(cdb_t *cdb, cdb_callback cb, void *param);
CDB_API int cdb_add(cdb_t *cdb, const cdb_buffer_t *key, const cdb_buffer_t *value);
CDB_API int cdb_tests(cdb_file_operators_t *ops, cdb_allocator_t *allocator);

#ifdef __cplusplus
}
#endif
#endif
