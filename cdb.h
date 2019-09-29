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

struct cdb;
typedef struct cdb cdb_t;
typedef uint32_t cdb_word_t;

enum { CDB_SEEK_START, CDB_SEEK_CURRENT, CDB_SEEK_END, };
enum { CDB_RO_MODE, CDB_RW_MODE };

typedef struct {
	void *(*malloc)(void *arena, size_t length);
	void *(*realloc)(void *arena, void *pointer, size_t length);
	int (*free)(void *arena, void *pointer);
	void *arena;
} cdb_allocator_t; /* custom allocator interface; mostly used for creation of data-base */

typedef struct {
	cdb_word_t (*read)(void *file, void *buf, size_t length);
	cdb_word_t (*write)(void *file, void *buf, size_t length); /* (conditionally option) needed for creation only */
	int (*seek)(void *file, long offset, long whence);
	void *(*open)(const char *name, int mode);
	int (*close)(void *file);
	int (*flush)(void *file); /* (optional) called at end of successful creation */
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

/* All functions return: < 0 on failure, 0 on success/not found, 1 on found */
CDB_API int cdb_open(cdb_t **cdb, cdb_file_operators_t *ops, cdb_allocator_t *allocator, int create, const char *file);
CDB_API int cdb_close(cdb_t *cdb);  /* free cdb, close (and write to disk if in create mode) */
CDB_API int cdb_get(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value);
CDB_API int cdb_get_record(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value, long record);
CDB_API int cdb_foreach(cdb_t *cdb, cdb_callback cb, void *param);
CDB_API int cdb_add(cdb_t *cdb, const cdb_buffer_t *key, const cdb_buffer_t *value);
CDB_API int cdb_seek(cdb_t *cdb, long position, long whence);
CDB_API int cdb_read_word(cdb_t *cdb, cdb_word_t *word);
CDB_API int cdb_read_word_pair(cdb_t *cdb, cdb_word_t *w1, cdb_word_t *w2);
CDB_API int cdb_tests(cdb_file_operators_t *ops, cdb_allocator_t *allocator, const char *test_file);

/* returns: number of characters read in, zero on error or length == 0 */
CDB_API cdb_word_t cdb_read(cdb_t *cdb, void *buf, size_t length);

#ifdef __cplusplus
}
#endif
#endif
