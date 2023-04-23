/* Consult the "readme.md" file in the repository for a detailed
 * description of the API and the internals. */
#ifndef CDB_H
#define CDB_H
#ifdef __cplusplus
extern "C" {
#endif

#define CDB_PROJECT "Constant Database"
#define CDB_AUTHOR  "Richard James Howe"
#define CDB_EMAIL   "howe.r.j.89@gmail.com"
#define CDB_LICENSE "The Unlicense"
#define CDB_REPO    "https://github.com/howerj/cdb"

#include <stddef.h>
#include <stdint.h>

#ifndef CDB_API
#define CDB_API /* Used to apply attributes to exported functions */
#endif

#ifndef CDB_WORD_T
typedef uint64_t cdb_word_t; /* valid sizes: uint64_t, uint32_t, uint16_t */
#endif

struct cdb;
typedef struct cdb cdb_t;

enum { CDB_RO_MODE, CDB_RW_MODE, }; /* passed to "open" in the "mode" option */

typedef struct {
	void *(*allocator)(void *arena, void *ptr, size_t oldsz, size_t newsz);
	cdb_word_t (*hash)(const uint8_t *data, size_t length); /* hash function: NULL defaults to djb hash */
	int (*compare)(const void *a, const void *b, size_t length); /* key comparison function: NULL defaults to memcmp */
	cdb_word_t (*read)(void *file, void *buf, size_t length); /* always needed, read from a resource */
	cdb_word_t (*write)(void *file, void *buf, size_t length); /* (conditionally optional) needed for db creation only, write to a resource */
	int (*seek)(void *file, uint64_t offset); /* "tell" is not needed as we keep track of the file position internally */
	void *(*open)(const char *name, int mode); /* open up a resource, which may or may not be a file, for reading (mode = CDB_RO_MODE) or read/write (mode = CDB_RW_MODE) */
	int (*close)(void *file); /* close a resource opened up with "open" */
	int (*flush)(void *file); /* (optional) called at end of successful creation */

	void *arena;       /* used for 'arena' argument for the allocator, can be NULL if allocator allows it */
	cdb_word_t offset; /* starting offset for CDB file if not at beginning of file */
	unsigned size;     /* Either 0 (defaults 32), 16, 32 or 64, but cannot be bigger than 'sizeof(cdb_word_t)*8' in any case */
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
CDB_API int cdb_open(cdb_t **cdb, const cdb_options_t *ops, int create, const char *file); /* arena may be NULL, allocator must be present */
CDB_API int cdb_close(cdb_t *cdb);  /* free cdb, close handles (and write to disk if in create mode) */
CDB_API int cdb_read(cdb_t *cdb, void *buf, cdb_word_t length); /* Returns error code not length! Not being able to read "length" bytes is an error! */
CDB_API int cdb_add(cdb_t *cdb, const cdb_buffer_t *key, const cdb_buffer_t *value); /* do not call cdb_read and/or cdb_seek in open mode */
CDB_API int cdb_seek(cdb_t *cdb, cdb_word_t position);
CDB_API int cdb_foreach(cdb_t *cdb, cdb_callback cb, void *param);
CDB_API int cdb_read_word_pair(cdb_t *cdb, cdb_word_t *w1, cdb_word_t *w2);
CDB_API int cdb_get(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value);
CDB_API int cdb_lookup(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value, uint64_t record);
CDB_API int cdb_count(cdb_t *cdb, const cdb_buffer_t *key, uint64_t *count);
CDB_API int cdb_status(cdb_t *cdb); /* returns CDB error status */
CDB_API int cdb_version(unsigned long *version); /* version number in x.y.z format, z = LSB, MSB is library info */
CDB_API int cdb_tests(const cdb_options_t *ops, const char *test_file);

CDB_API uint64_t cdb_prng(uint64_t s[2]); /* "s" is PRNG state, you can set it to any value you like to seed */
CDB_API cdb_word_t cdb_hash(const uint8_t *data, size_t length); /* hash used by original CDB program */

#ifdef __cplusplus
}
#endif
#endif
