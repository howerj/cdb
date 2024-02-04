/* Author: Richard James Howe
 * License: Public Domain / The Unlicense
 * Email: howe.r.j.89@gmail.com
 * Repo: https://github.com/howerj/cdb */
#ifndef CDB_EXTRA_H
#define CDB_EXTRA_H

#include "cdb.h"
#include <stdbool.h>
#include <stdio.h>

typedef struct {
	/* We could add an unsigned integer field, used to store bits,
	 * with fields for:
	 * - Indicating ownership of the key-buffer, and value-buffer
	 *   (e.g. Whether it can be free()'d or not).
	 * - Whether the key is a duplicate.
	 * Amongst other things.
	 *
	 * We *could* do this, or potentially set flags in `cdb_hash_t`
	 * to disable duplicate keys and to indicate ownership of the
	 * buffer contents.
	 *
	 * Adding another field here would make the hash much bigger
	 * and potentially slower to traverse. 
	 *
	 * There are some small value optimizations that could be done
	 * as well (for example storing strings in pointers if the length
	 * field is less or equal to the pointer size) that also affect
	 * ownership of the string. Who owns what turns about to be a
	 * big question! */
	cdb_buffer_t key, 
		     value;
} cdb_hash_entry_t; /* should possibly be split up for performance reasons */

typedef struct {
	cdb_allocator_fn alloc;  /* Allocator for this hash table; set during initialization */
	void *arena;             /* Arena passed to allocator; set during initialization */
	cdb_hash_entry_t *items; /* List of hash table items, may be NULL depending on `length`, never access directly */
	size_t used,             /* Number of `items` in use in hash table */
	       length;           /* Length of `items` field in records */
	unsigned error: 1,       /* A fatal error has occurred */
		 alloc_key: 1,
		 alloc_val: 1;
} cdb_hash_t; /* Hash table data structure */

typedef struct {
	char *arg;   /* parsed argument */
	long narg;   /* converted argument for '#' */
	int index,   /* index into argument list */
	    option,  /* parsed option */
	    reset;   /* set to reset */
	FILE *error, /* error stream to print to (set to NULL to turn off */
	     *help;  /* if set, print out all options and return */
	char *place; /* internal use: scanner position */
	int  init;   /* internal use: initialized or not */
} cdb_getopt_t;     /* getopt clone; with a few modifications */

enum { /* used with `cdb_callbacks_t` structure */
	CDB_OPTIONS_INVALID_E, /* default to invalid if option type not set */
	CDB_OPTIONS_BOOL_E,    /* select boolean `b` value in union `v` */
	CDB_OPTIONS_LONG_E,    /* select numeric long `n` value in union `v` */
	CDB_OPTIONS_STRING_E,  /* select string `s` value in union `v` */
};

typedef struct { /* Used for parsing key=value strings (strings must be modifiable and persistent) */
	char *opt,  /* key; name of option */
	     *help; /* help string for option */
	union { /* pointers to values to set */
		bool *b; 
		long *n; 
		char **s; 
	} v; /* union of possible values, selected on `type` */
	int type; /* type of value, in following union, e.g. CDB_OPTIONS_LONG_E. */
} cdb_getopt_options_t; /* N.B. This could be used for saving configurations as well as setting them */

int cdb_get_long(cdb_t *cdb, char *key, long *value);
char *cdb_get_string(cdb_t *cdb, char *key);
char *cdb_get_buffer_base64(cdb_t *cdb, char *key);
int cdb_get_buffer(cdb_t *cdb, char *key, cdb_buffer_t *value);
int cdb_get_bool(cdb_t *cdb, char *key);
int cdb_get_hash(cdb_t *cdb, cdb_hash_t *hash);

int cdb_add_string(cdb_t *cdb, const char *key, const char *value);
int cdb_add_long(cdb_t *cdb, const char *key, long value);
int cdb_add_buffer(cdb_t *cdb, const char *key, const cdb_buffer_t *value);
int cdb_add_buffer_base64(cdb_t *cdb, const char *key, const char *base64_string);
int cdb_add_bool(cdb_t *cdb, char *key, bool on);
int cdb_add_hash(cdb_t *cdb, cdb_hash_t *hash);

int cdb_are_keys_unique(cdb_t *cdb);
int cdb_is_sorted(cdb_t *cdb, bool onkeys, bool ignorecase, int direction);
int cdb_is_sorted_fn(cdb_t *cdb, int onkeys, int (*cmp)(cdb_t *cdb, void *param, const cdb_file_pos_t *prev, const cdb_file_pos_t *cur), void *param);

int cdb_lookup_allocate(cdb_t *cdb, const cdb_buffer_t *key, uint8_t **value, size_t *length, uint64_t record, int reuse);
int cdb_flag(const char *v);
int cdb_convert(const char *n, int base, long *out);
int cdb_options_help(cdb_getopt_options_t *os, size_t olen, FILE *out);
int cdb_options_set(cdb_getopt_options_t *os, size_t olen, char *kv, FILE *error);
int cdb_getopt(cdb_getopt_t *opt, const int argc, char *const argv[], const char *fmt);

int cdb_base64_decode(const unsigned char *ibuf, const size_t ilen, unsigned char *obuf, size_t *olen);
int cdb_base64_encode(const unsigned char *ibuf, size_t ilen, unsigned char *obuf, size_t *olen);

void cdb_reverse_char_array(char * const r, const size_t length);

int cdb_hash_create(cdb_allocator_fn alloc, void *arena, cdb_hash_t **hash);
int cdb_hash_destroy(cdb_hash_t *h);
int cdb_hash_exists(cdb_hash_t *h, const cdb_buffer_t *key);
int cdb_hash_get(cdb_hash_t *h, const cdb_buffer_t *key, cdb_buffer_t **value);
int cdb_hash_set(cdb_hash_t *h, const cdb_buffer_t *key, const cdb_buffer_t *value);
int cdb_hash_are_keys_unique(cdb_hash_t *h);
int cdb_hash_delete(cdb_hash_t *h, const cdb_buffer_t *key);
int cdb_hash_foreach(cdb_hash_t *h, int (*cb)(void *param, const cdb_buffer_t *key, const cdb_buffer_t *value), void *param);
int cdb_hash_count(cdb_hash_t *h, size_t *count);

int cdb_hash_set_buffer(cdb_hash_t *h, const char *key, cdb_buffer_t *value);
int cdb_hash_get_buffer(cdb_hash_t *h, const char *key, cdb_buffer_t *value);
int cdb_hash_set_long(cdb_hash_t *h, const char *key, long value);
int cdb_hash_get_long(cdb_hash_t *h, const char *key, long *value);
int cdb_hash_set_string(cdb_hash_t *h, const char *key, const char *string);
int cdb_hash_get_string(cdb_hash_t *h, const char *key, char **string);
int cdb_hash_set_bool(cdb_hash_t *h, const char *key, bool value);
int cdb_hash_get_bool(cdb_hash_t *h, const char *key, bool *value);

int cdb_isalnum(int ch);
int cdb_isalpha(int ch);
int cdb_isascii(int ch);
int cdb_isblank(int ch);
int cdb_iscntrl(int ch);
int cdb_isdigit(int ch);
int cdb_isgraph(int ch);
int cdb_islower(int ch);
int cdb_isprint(int ch);
int cdb_ispunct(int ch);
int cdb_isspace(int ch);
int cdb_isupper(int ch);
int cdb_isxdigit(int ch);
int cdb_tolower(int ch);
int cdb_toupper(int ch);

int cdb_istrcmp(const char *a, const char *b); /* ASCII only */

int cdb_extra_tests(const cdb_callbacks_t *ops, const char *testfile);

#endif
