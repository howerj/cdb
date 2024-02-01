// TODO: Write hash table to disk, load from disk, deal with multiple values,
// foreach, iterator, sorting, adding long/base64/string/buffers to hash
// TODO: For Get/Set routines; Base-64 encoded string handling, JSON handling (using jsmn.h), Hashes, ...
// TODO: Make index on a sorted database for a binary search?
// TODO: Replace getopt in main, rationalize arguments
// TODO: Private header for `cdb_t`
// TODO: Documentation, integrate into library along with host.c and mem.c
// TODO: Unit tests
// TODO: Add hash function seed, or `param` field to hash function callback, or
// document problem in `readme.md`.
// TODO: Function to load from cdb, create a new cdb and keep open for writing/adding new keys
// TODO: Generate a perfect hash table from a cdb/hash
// TODO: Add lock functions, or note about them in readme.md, to callbacks
// (needed for creation only, really).
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include "extra.h"

#define CDB_NELEMS(X) (sizeof((X)) / sizeof((X)[0]))

static void *fn_allocate(cdb_allocator_fn alloc, void *arena, const size_t sz) {
	assert(alloc);
	return alloc(arena, NULL, 0, sz);
}

static int fn_free(cdb_allocator_fn alloc, void *arena, void *ptr) {
	assert(alloc);
	(void)alloc(arena, ptr, 0, 0);
	return 0;
}

void cdb_reverse_char_array(char * const r, const size_t length) {
	assert(r);
	const size_t last = length - 1;
	for (size_t i = 0; i < length / 2ul; i++) {
		const char t = r[i];
		r[i] = r[last - i];
		r[last - i] = t;
	}
}

/* These is_X functions are defined to avoid the use of locale dependent functions 
 *
 * Nested functions, even without solving the upwards or downward
 * funarg problems and just banning references to variables in the
 * containing scope, would be useful for these small functions. 
 */
int cdb_isgraph(const int ch) { return ch > 32 && ch < 127; }
int cdb_islower(const int ch) { return ch >= 97 && ch <= 122; }
int cdb_isupper(const int ch) { return ch >= 65 && ch <= 90; }
int cdb_isdigit(const int ch) { return ch >= 48 && ch <= 57; }
int cdb_isxdigit(const int ch) { return (ch >= 65 && ch <= 70) || (ch >= 97 && ch <= 102) || cdb_isdigit(ch); }
int cdb_toupper(const int ch) { return cdb_islower(ch) ? ch ^ 0x20 : ch; }
int cdb_tolower(const int ch) { return cdb_isupper(ch) ? ch ^ 0x20 : ch; }

static int cdb_istrcmp(const char *a, const char *b) {
	for (size_t i = 0; ; i++) {
		const int ach = cdb_toupper(a[i]);
		const int bch = cdb_toupper(b[i]);
		const int diff = ach - bch;
		if (!ach || diff)
			return diff;
	}
	return 0;
}

static inline int within(uint64_t value, uint64_t lo, uint64_t hi) {
	return (value >= lo) && (value <= hi);
}

static inline void u64_to_str(char b[64], uint64_t u, const uint64_t base) {
	assert(b);
	assert(within(base, 2, 16));
	unsigned i = 0;
	do {
		const uint64_t q = u % base;
		const uint64_t r = u / base;
		b[i++] = q["0123456789ABCDEF"];
		u = r;
	} while (u);
	cdb_reverse_char_array(b, i);
	b[i] = '\0';
	assert(i >= 1);
}

static inline void i64_to_str(char b[65], int64_t s, const uint64_t base) {
	assert(b);
	if (s < 0) {
		b[0] = '-';
		s = -s;
		u64_to_str(b + 1, s, base);
		return;
	}
	u64_to_str(b, s, base);
}

static inline int digit(int ch, int base) {
	int r = -1;
	if (ch >= '0' && ch <= '9')
		r = ch - '0';
	if (ch >= 'a' && ch <= 'f')
		r = ch - 'a' + 10;
	if (ch >= 'A' && ch <= 'F')
		r = ch - 'A' + 10;
	if (r >= base)
		r = -1;
	return r;
}

static int str_to_u64(const char *str, const size_t length, const uint64_t base, uint64_t *out) {
	assert(str);
	assert(out);
	assert(within(base, 2, 16));
	*out = 0;
	if (length == 0)
		return -1;
	uint64_t t = 0;
	for (size_t i = 0; i < length; i++) {
		const int dg = digit(str[i], base);
		if (dg < 0)
			return -1;
		const uint64_t nt1 = t * base;
		if (nt1 < t) /* overflow */
			return -1;
		const uint64_t nt2 = nt1 + dg;
		if (nt2 < t) /* overflow */
			return -1;
		t = nt2;
	}
	*out = t;
	return 0;
}

static int str_to_i64(const char *str, const size_t length, const uint64_t base, int64_t *out) {
	assert(str);
	assert(out);
	assert(within(base, 2, 16));
	const int negative = length > 0 && str[0] == '-';
	uint64_t t = 0;
	if (str_to_u64(str + negative, length - negative, base, &t) < 0)
		return -1;
	int64_t o = t;
	if (negative)
		o = -o;
	*out = o;
	return 0;
}

// TODO: Encoded size, account for padding / linebreaks
static inline size_t base64_decoded_size(const size_t sz) {
	assert((sz * 3ull) >= sz);
	return (sz * 3ull) / 4ull;
}

int cdb_base64_decode(const unsigned char *ibuf, const size_t ilen, unsigned char *obuf, size_t *olen) {
	assert(ibuf);
	assert(obuf);
	assert(olen);

	enum { WS = 64u, /* white space */ EQ = 65u, /*equals*/ XX = 66u, /* invalid */ };

	static const unsigned char d[] = { /* 0-63 = valid chars, 64-66 = special */
		XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, WS, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX,
		XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, 62, XX, XX, XX, 63, 52, 53,
		54, 55, 56, 57, 58, 59, 60, 61, XX, XX, XX, EQ, XX, XX, XX, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
		10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, XX, XX, XX, XX, XX, XX, 26, 27, 28,
		29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, XX, XX,
		XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX,
		XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX,
		XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX,
		XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX,
		XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX, XX,
		XX, XX, XX, XX, XX, XX
	};

	unsigned iter = 0;
	uint32_t buf = 0;
	size_t len = 0;

	for (const unsigned char *end = ibuf + ilen; ibuf < end; ibuf++) {
		const unsigned char c = d[(int)(*ibuf)];

		switch (c) {
		case WS: continue;   /* skip whitespace */
		case XX: return -1;  /* invalid input, return error */
		case EQ:			 /* pad character, end of data */
			ibuf = end;
			continue;
		default:
			assert(c < 64);
			buf = (buf << 6) | c;
			iter++;
			/* If the buffer is full, split it into bytes */
			if (iter == 4) {
				if ((len += 3) > *olen) /* buffer overflow */
					return -1;
				*(obuf++) = (buf >> 16) & 0xFFul;
				*(obuf++) = (buf >> 8) & 0xFFul;
				*(obuf++) = buf & 0xFFul;
				buf  = 0;
				iter = 0;
			}
		}
	}

	if (iter == 3) {
		if ((len += 2) > *olen)
			return -1;	/* buffer overflow */
		*(obuf++) = (buf >> 10) & 0xFFul;
		*(obuf++) = (buf >>  2) & 0xFFul;
	} else if (iter == 2) {
		if (++len > *olen)
			return -1;	/* buffer overflow */
		*(obuf++) = (buf >> 4) & 0xFFul;
	}

	*olen = len; /* modify to reflect the actual size */
	return 0;
}

static inline size_t base64_encoded_size(const size_t sz) {
	return 4ull * ((sz + 2ull) / 3ull);
}

int cdb_base64_encode(const unsigned char *ibuf, size_t ilen, unsigned char *obuf, size_t *olen) {
	assert(ibuf);
	assert(olen);

	static int mod_table[] = { 0, 2, 1, };

	static char lookup[] = {
		'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
		'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
		'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
		'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
		'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
		'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
		'w', 'x', 'y', 'z', '0', '1', '2', '3',
		'4', '5', '6', '7', '8', '9', '+', '/'
	};

	const size_t osz = base64_encoded_size(ilen);

	if (*olen < osz)
		return -1;

	*olen = osz;

	for (size_t i = 0, j = 0; i < ilen;) {
		const uint32_t a = i < ilen ? ibuf[i++] : 0;
		const uint32_t b = i < ilen ? ibuf[i++] : 0;
		const uint32_t c = i < ilen ? ibuf[i++] : 0;
		const uint32_t triple = (a << 0x10) | (b << 0x08) | c;

		obuf[j++] = lookup[(triple >> 3 * 6) & 0x3F];
		obuf[j++] = lookup[(triple >> 2 * 6) & 0x3F];
		obuf[j++] = lookup[(triple >> 1 * 6) & 0x3F];
		obuf[j++] = lookup[(triple >> 0 * 6) & 0x3F];
	}

	for (int i = 0; i < mod_table[ilen % 3]; i++)
		obuf[*olen - 1 - i] = '=';

	return 0;
}

static int cdb_load_file_pos_to_buffer(cdb_t *cdb, const cdb_file_pos_t *pos, cdb_buffer_t *buf) {
	assert(cdb);
	assert(pos);
	assert(buf);
	buf->length = 0;
	buf->buffer = NULL;

	if ((pos->length + 1) < pos->length)
		return -1;
	if (cdb_seek(cdb, pos->position) < 0)
		return -1;
	char *v = cdb_allocate(cdb, pos->length + 1);
	if (!v)
		return -1;
	if (cdb_read(cdb, v, pos->length) < 0) {
		(void)cdb_free(cdb, v);
		return -1;
	}
	buf->length = pos->length;
	buf->buffer = v;
	v[buf->length] = 0;
	return 0;
}

int cdb_lookup_allocate(cdb_t *cdb, const cdb_buffer_t *key, uint8_t **value, size_t *length, uint64_t record, int reuse) {
	assert(cdb);
	assert(key);
	assert(value);
	assert(length);
	cdb_file_pos_t val = { .position = 0, };

	uint8_t *v = *value;
	size_t l = *length;

	if (!reuse) {
		*value = 0;
		*length = 0;
	}

	const int r = cdb_lookup(cdb, key, &val, record);
	if (r < 0)
		return -1;
	if ((val.length + 1) < val.length)
		return -1;

	if (reuse) {
		/* if too small reallocate */
		if (l < val.length) {
			uint8_t *n = cdb_reallocate(cdb, v, val.length);
			if (!n)
				return -1;
			v = n;
			l = val.length;
		}
	} else {
		if (!(v = cdb_allocate(cdb, val.length + 1)))
			return -1;
		l = val.length;
		v[val.length] = 0;
	}

	if (cdb_seek(cdb, val.position) < 0)
		goto fail;
	if (cdb_read(cdb, v, val.length) < 0)
		goto fail;

	*value = v;
	*length = l;

	return 0;
fail:
	*value = v; /* caller must free on error */
	*length = 0;
	return -1;
}

static const char *cdb_hash_tombstone_value = "TOMBSTONE";

static int cdb_hash_entry_valid(cdb_hash_entry_t *e) {
	assert(e);
	/*cdb_implies(e->key.buffer, e->value.buffer);*/
	if (e->key.buffer == cdb_hash_tombstone_value)
		return 0;
	if (e->key.buffer != NULL)
		return 1;
	return 0;
}

char *cdb_get_string(cdb_t *cdb, char *key) {
	assert(cdb);
	assert(key);
	uint8_t *v = NULL;
	cdb_buffer_t k = { .buffer = key, .length = strlen(key), };
	size_t vlen = 0;
	const int r = cdb_lookup_allocate(cdb, &k, &v, &vlen, 0, 0);
	if (r < 0) {
		(void)cdb_free(cdb, v);
		return NULL;
	}
	return (char*)v;
}

int cdb_get_long(cdb_t *cdb, char *key, long *value) {
	assert(cdb);
	assert(key);
	assert(value);
	cdb_buffer_t k = { .buffer = key, .length = strlen(key), };
	cdb_file_pos_t v = { .position = 0, };
	*value = 0;
	if (cdb_get(cdb, &k, &v) < 0)
		return -1;
	if (v.length == 0 || v.length > (64 + 1)) /* instead of 64 it should be `ceil(log_base(LONG_MAX)) + 1` */
		return -1;
	char b[64 + 1 + 1] = { 0, };
	if (cdb_seek(cdb, v.position) < 0)
		return -1;
	if (cdb_read(cdb, b, v.length) < 0)
		return -1;
	int64_t i = 0; 
	if (str_to_i64(b, v.length, 10, &i) < 0)
		return -1;
	*value = i;
	return 0;
}

char *cdb_get_buffer_base64(cdb_t *cdb, char *key) {
	assert(cdb);
	assert(key);
	return NULL;
}

int cdb_get_buffer(cdb_t *cdb, char *key, cdb_buffer_t *value) {
	assert(cdb);
	assert(key);
	assert(value);
	cdb_buffer_t k = { .buffer = (char*)key, .length = strlen(key), };
	cdb_buffer_t v = { .buffer = NULL, };
	*value = v;
	if (cdb_lookup_allocate(cdb, &k, (uint8_t**)&v.buffer, &v.length, 0, 0) < 0) {
		(void)cdb_free(cdb, v.buffer);
		return -1;
	}
	*value = v;
	return 0;
}

int cdb_get_bool(cdb_t *cdb, char *key) {
	assert(cdb);
	assert(key);
	cdb_buffer_t k = { .buffer = (char*)key, .length = strlen(key), };
	cdb_file_pos_t v = { .position = 0, };
	if (cdb_get(cdb, &k, &v) < 0)
		return -1;
	char b[8] = { 0, };
	if (v.length == 0 || v.length > (sizeof (b) - 1))
		return -1;
	if (cdb_seek(cdb, v.position) < 0)
		return -1;
	if (cdb_read(cdb, b, v.length) < 0)
		return -1;
	return cdb_flag(b);
}

int cdb_get_hash(cdb_t *cdb, cdb_hash_t *hash) {
	assert(cdb);
	assert(hash);
	return 0;
}

int cdb_add_string(cdb_t *cdb, const char *key, const char *value) {
	assert(cdb);
	assert(key);
	assert(value);
	cdb_buffer_t k = { .buffer = (char*)key,   .length = strlen(key),   };
	cdb_buffer_t v = { .buffer = (char*)value, .length = strlen(value), };
	return cdb_add(cdb, &k, &v);
}

int cdb_add_long(cdb_t *cdb, const char *key, long value) {
	assert(cdb);
	assert(key);
	char buf[64 + 1/* '+'/'-' */ + 1 /* ASCII NUL */] = { 0, };
	i64_to_str(buf, value, 10);
	cdb_buffer_t k = { .buffer = (char*)key, .length = strlen(key), };
	cdb_buffer_t v = { .buffer = buf,        .length = strlen(buf), };
	return cdb_add(cdb, &k, &v);
}

int cdb_add_bool(cdb_t *cdb, char *key, bool on) {
	assert(cdb);
	assert(key);
	char *b = on ? "1" : "0";
	cdb_buffer_t k = { .buffer = (char*)key, .length = strlen(key), };
	cdb_buffer_t v = { .buffer = b, .length = 1, };
	return cdb_add(cdb, &k, &v);
}

int cdb_add_buffer(cdb_t *cdb, const char *key, const cdb_buffer_t *value) {
	assert(cdb);
	assert(key);
	assert(value);
	cdb_buffer_t k = { .buffer = (char*)key, .length = strlen(key), };
	return cdb_add(cdb, &k, value);
}

// TODO: Is this right? Should be char*/length pair instead which we base64
// encode?
int cdb_add_buffer_base64(cdb_t *cdb, const char *key, const char *base64_string) {
	assert(cdb);
	assert(key);
	assert(base64_string);
	return 0;
}

int cdb_add_hash(cdb_t *cdb, cdb_hash_t *hash) {
	assert(cdb);
	assert(hash);
	for (size_t i = 0; i < hash->length; i++) {
		cdb_hash_entry_t *e = &hash->items[i];
		if (!cdb_hash_entry_valid(e))
			continue;
		if (cdb_add(cdb, &e->key, &e->value) < 0)
			return -1;
	}
	return 0;
}

int cdb_are_keys_unique(cdb_t *cdb) {
	assert(cdb);
	int r = 0;
	cdb_iterator_t it = { .key = { .position = 0, }, };
	for (; (r = cdb_iterate(cdb, &it)) > 0;) {
		cdb_file_pos_t value = { .length = 0, };
		cdb_buffer_t key = { .buffer = NULL, };
		if (cdb_load_file_pos_to_buffer(cdb, &it.key, &key) < 0)
			return -1;
		const int l = cdb_lookup(cdb, &key, &value, 1);
		if (cdb_free(cdb, key.buffer) < 0)
			return -1;
		if (l < 0)
			return -1;
		if (l > 0)
			return 0;
	}
	if (r < 0)
		return -1;
	return 1;
}

int cdb_is_sorted_fn(cdb_t *cdb, int onkeys, int (*cmp)(void *param, const cdb_file_pos_t *prev, const cdb_file_pos_t *cur), void *param) {
	assert(cdb);
	assert(cmp);
	int r = 0, v = 0;
	cdb_iterator_t it = { .key = { .position = 0, }, };
	cdb_file_pos_t *prev = NULL;
	for (; (r = cdb_iterate(cdb, &it)) > 0;) {
		cdb_file_pos_t cur = onkeys ? it.key : it.value;
		const int c = cmp(param, prev, &cur);
		if ((c < 0 && v > 0) || (c > 0 && v < 0))
			return -1;
	}
	if (r < 0)
		return -1;
	return 1;
}

/* TODO: Case insensitive version? */
static int cdb_alpha_is_sorted_callback(void *param, const cdb_file_pos_t *prev, const cdb_file_pos_t *cur) {
	assert(cur);
	(void)param;
	if (prev == NULL)
		return 0;
	/*char p[128] = { 0, }, c[128] = { 0, };*/
	return 0;
}

int cdb_is_sorted(cdb_t *cdb, int onkeys) {
	assert(cdb);
	return cdb_is_sorted_fn(cdb, onkeys, cdb_alpha_is_sorted_callback, NULL);
}

cdb_hash_t *cdb_hash_create(cdb_allocator_fn alloc, void *arena) {
	assert(alloc);
	return NULL;
}

int cdb_hash_destroy(cdb_hash_t *h) {
	assert(h);
	int r = 0;
	size_t used = 0, i = 0;
	/*cdb_implies(h->length, h->items); */
	for (i = 0; i < h->length; i++) {
		const cdb_buffer_t empty = { .buffer = NULL, };
		cdb_hash_entry_t *e = &h->items[i];
		if (!cdb_hash_entry_valid(e))
			continue;
		used++;
		if (fn_free(h->alloc, h->arena, e->key.buffer) < 0)
			r = -1;
		if (fn_free(h->alloc, h->arena, e->value.buffer) < 0)
			r = -1;
		e->key = empty;
		e->value = empty;
	}
	if (fn_free(h->alloc, h->arena, h->items) < 0)
		r = -1;
	assert(used == h->used);
	h->used = 0;
	h->length = 0;
	h->items = NULL;
	if (fn_free(h->alloc, h->arena, h) < 0)
		r = -1;
	return r;
}

//int cdb_hash_exists(cdb_hash_t *h, const cdb_buffer_t *key);
//int cdb_hash_lookup()

int cdb_hash_get(cdb_hash_t *h, const cdb_buffer_t *key, cdb_buffer_t **value) {
	assert(h);
	assert(key);
	assert(value);
	*value = NULL;
	return 0;
}


int cdb_hash_set(cdb_hash_t *h, const cdb_buffer_t *key, const cdb_buffer_t *value) {
	assert(h);
	assert(key);
	assert(value);
	/* TODO: Uniqueness check */
	return 0;
}


int cdb_hash_are_keys_unique(cdb_hash_t *h) {
	assert(h);
	return 0;
}


int cdb_hash_delete(cdb_hash_t *h, const cdb_buffer_t *key) {
	assert(h);
	assert(key);
	/* tombstone */
	return 0;
}


int cdb_hash_foreach(cdb_hash_t *h, int (*cb)(void *param, const cdb_buffer_t *key, const cdb_buffer_t *value), void *param) {
	assert(h);
	assert(cb);
	for (size_t i = 0; i < h->length; i++) {
		cdb_hash_entry_t *e = &h->items[i];
		if (!cdb_hash_entry_valid(e))
			continue;
		const int r = cb(param, &e->key, &e->value);
		if (r < 0)
			return -1;
		if (r > 0) /* early termination */
			break;
	}
	return 0;
}

int cdb_hash_count(cdb_hash_t *h, size_t *count) {
	assert(h);
	assert(count);
	*count = h->used;
	return 0;
}

int cdb_flag(const char *v) {
	assert(v);

	static char *y[] = { "1", "yes", "on",  "true",  };
	static char *n[] = { "0", "no",  "off", "false", };

	for (size_t i = 0; i < CDB_NELEMS(y); i++) {
		if (!cdb_istrcmp(y[i], v))
			return 1;
		if (!cdb_istrcmp(n[i], v))
			return 0;
	}
	return -1;
}

int cdb_convert(const char *n, int base, long *out) {
	assert(n);
	assert(out);
	*out = 0;
	char *endptr = NULL;
	errno = 0;
	const long r = strtol(n, &endptr, base);
	if (*endptr)
		return -1;
	if (errno == ERANGE)
		return -1;
	*out = r;
	return 0;
}

int cdb_options_help(cdb_getopt_options_t *os, size_t olen, FILE *out) {
	assert(os);
	assert(out);
	for (size_t i = 0; i < olen; i++) {
		cdb_getopt_options_t *o = &os[i];
		assert(o->opt);
		const char *type = "unknown";
		switch (o->type) {
		case CDB_OPTIONS_BOOL_E: type = "bool"; break;
		case CDB_OPTIONS_LONG_E: type = "long"; break;
		case CDB_OPTIONS_STRING_E: type = "string"; break;
		case CDB_OPTIONS_INVALID_E: /* fall-through */
		default: type = "invalid"; break;
		}
		if (fprintf(out, " * `%s`=%s: %s\n", o->opt, type, o->help ? o->help : "") < 0)
			return -1;
	}
	return 0;
}

int cdb_options_set(cdb_getopt_options_t *os, size_t olen, char *kv, FILE *error) {
	assert(os);
	char *k = kv, *v = NULL;
	if ((v = strchr(kv, '=')) == NULL || *v == '\0') {
		if (error)
			(void)fprintf(error, "invalid key-value format: %s\n", kv);
		return -1;
	}
	*v++ = '\0'; /* Assumes `kv` is writeable! */

	cdb_getopt_options_t *o = NULL;
	for (size_t i = 0; i < olen; i++) {
		cdb_getopt_options_t *p = &os[i];
		if (!strcmp(p->opt, k)) { o = p; break; }
	}
	if (!o) {
		if (error)
			(void)fprintf(error, "option `%s` not found\n", k);
		return -1;
	}

	switch (o->type) {
	case CDB_OPTIONS_BOOL_E: {
		const int r = cdb_flag(v);
		assert(r == 0 || r == 1 || r == -1);
		if (r < 0) {
			if (error)
				(void)fprintf(error, "invalid flag in option `%s`: `%s`\n", k, v);
			return -1;
		}
		*o->v.b = !!r;
		break;
	}
	case CDB_OPTIONS_LONG_E: { 
		const int r = cdb_convert(v, 0, o->v.n); 
		if (r < 0) {
			if (error)
				(void)fprintf(error, "invalid number in option `%s`: `%s`\n", k, v);
			return -1;
		}
		break; 
	}
	case CDB_OPTIONS_STRING_E: { *o->v.s = v; /* Assumes `kv` is persistent! */ break; }
	default: return -1;
	}
	
	return 0;
}

/* Adapted from: <https://stackoverflow.com/questions/10404448>, this
 * could be extended to accept an array of options instead, or
 * perhaps it could be turned into a variadic functions,
 * that is not needed here. The function and structure should be turned
 * into a header only library. 
 *
 * This version handles parsing numbers with '#' and strings with ':'.
 *
 * Return value:
 *
 * - "-1": Finished parsing (end of options or "--" option encountered).
 * - ":": Missing argument (either number or string).
 * - "?": Bad option.
 * - "!": Bad I/O (e.g. `printf` failed).
 * - "#": Bad numeric argument (out of range, not a number, ...)
 *
 * Any other value should correspond to an option.
 *
 */
int cdb_getopt(cdb_getopt_t *opt, const int argc, char *const argv[], const char *fmt) {
	assert(opt);
	assert(fmt);
	assert(argv);
	enum { BADARG_E = ':', BADCH_E = '?', BADIO_E = '!', BADNUM_E = '#', OPTEND_E = -1, };

#define CDB_GETOPT_NEEDS_ARG(X) ((X) == ':' || (X) == '#')

	if (opt->help) {
		for (int ch = 0; (ch = *fmt++);) {
			if (fprintf(opt->help, "\t-%c ", ch) < 0)
				return BADIO_E; 
			if (CDB_GETOPT_NEEDS_ARG(*fmt)) {
				if (fprintf(opt->help, "%s", *fmt == ':' ? "<string>" : "<number>") < 0)
					return BADIO_E;
				fmt++;
			}
			if (fputs("\n", opt->help) < 0)
				return BADIO_E;
		}
		return OPTEND_E;
	}

	if (!(opt->init)) {
		opt->place = ""; /* option letter processing */
		opt->init  = 1;
		opt->index = 1;
	}

	if (opt->reset || !*opt->place) { /* update scanning pointer */
		opt->reset = 0;
		if (opt->index >= argc || *(opt->place = argv[opt->index]) != '-') {
			opt->place = "";
			return OPTEND_E;
		}
		if (opt->place[1] && *++opt->place == '-') { /* found "--" */
			opt->index++;
			opt->place = "";
			return OPTEND_E;
		}
	}

	const char *oli = NULL; /* option letter list index */
	opt->option = *opt->place++;
	if (CDB_GETOPT_NEEDS_ARG(opt->option) || !(oli = strchr(fmt, opt->option))) { /* option letter okay? */
		 /* if the user didn't specify '-' as an option, assume it means -1.  */
		if (opt->option == '-')
			return OPTEND_E;
		if (!*opt->place)
			opt->index++;
		if (opt->error && !CDB_GETOPT_NEEDS_ARG(*fmt))
			if (fprintf(opt->error, "illegal option -- %c\n", opt->option) < 0)
				return BADIO_E;
		return BADCH_E;
	}

	const int o = *++oli;
	if (!CDB_GETOPT_NEEDS_ARG(o)) {
		opt->arg = NULL;
		if (!*opt->place)
			opt->index++;
	} else {  /* need an argument */
		if (*opt->place) { /* no white space */
			opt->arg = opt->place;
			if (o == '#') {
				if (cdb_convert(opt->arg, 0, &opt->narg) < 0) {
					if (opt->error)
						if (fprintf(opt->error, "option requires numeric value -- %s\n", opt->arg) < 0)
							return BADIO_E;
					return BADNUM_E;
				}
			}
		} else if (argc <= ++opt->index) { /* no arg */
			opt->place = "";
			if (CDB_GETOPT_NEEDS_ARG(*fmt)) {
				return BADARG_E;
			}
			if (opt->error)
				if (fprintf(opt->error, "option requires an argument -- %c\n", opt->option) < 0)
					return BADIO_E;
			return BADCH_E;
		} else	{ /* white space */
			opt->arg = argv[opt->index];
			if (o == '#') {
				if (cdb_convert(opt->arg, 0, &opt->narg) < 0) {
					if (opt->error)
						if (fprintf(opt->error, "option requires numeric value -- %s\n", opt->arg) < 0)
							return BADIO_E;
					return BADNUM_E;
				}
			}
		}
		opt->place = "";
		opt->index++;
	}
#undef CDB_GETOPT_NEEDS_ARG
	return opt->option; /* dump back option letter */
}


