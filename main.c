/* Program: Constant Database Driver
 * Author:  Richard James Howe
 * Email:   howe.r.j.89@gmail.com
 * License: Unlicense
 * Repo:    <https://github.com/howerj/cdb> */

#include "cdb.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNUSED(X)      ((void)(X))
#define MIN(X, Y)      ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y)      ((X) > (Y) ? (X) : (Y))
#define IO_BUFFER_SIZE (1024u)
#define DISTMAX        (10ul)

#ifdef _WIN32 /* Used to unfuck file mode for "Win"dows. Text mode is for losers. */
#include <windows.h>
#include <io.h>
#include <fcntl.h>
static void binary(FILE *f) { _setmode(_fileno(f), _O_BINARY); } /* only platform specific code... */
#else
static inline void binary(FILE *f) { UNUSED(f); }
#endif

typedef struct {
	unsigned long records;
	unsigned long total_key_length, total_value_length;
	unsigned long min_key_length, min_value_length;
	unsigned long max_key_length, max_value_length;
	unsigned long hash_start;
} cdb_statistics_t;

typedef struct {
	char *arg;   /* parsed argument */
	int error,   /* turn error reporting on/off */
	    index,   /* index into argument list */
	    option,  /* parsed option */
	    reset;   /* set to reset */
	char *place; /* internal use: scanner position */
	int  init;   /* internal use: initialized or not */
} cdb_getopt_t;      /* getopt clone; with a few modifications */

static unsigned verbose = 0;

static void info(const char *fmt, ...) {
	assert(fmt);
	if (verbose == 0)
		return;
	va_list ap;
	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
	(void)fputc('\n', stderr);
}

static void die(const char *fmt, ...) {
	assert(fmt);
	va_list ap;
	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
	(void)fputc('\n', stderr);
	exit(EXIT_FAILURE);
}

/* Adapted from: <https://stackoverflow.com/questions/10404448> */
static int cdb_getopt(cdb_getopt_t *opt, const int argc, char *const argv[], const char *fmt) {
	assert(opt);
	assert(fmt);
	assert(argv);
	enum { BADARG_E = ':', BADCH_E = '?' };

	if (!(opt->init)) {
		opt->place = ""; /* option letter processing */
		opt->init  = 1;
		opt->index = 1;
	}

	if (opt->reset || !*opt->place) { /* update scanning pointer */
		opt->reset = 0;
		if (opt->index >= argc || *(opt->place = argv[opt->index]) != '-') {
			opt->place = "";
			return -1;
		}
		if (opt->place[1] && *++opt->place == '-') { /* found "--" */
			opt->index++;
			opt->place = "";
			return -1;
		}
	}

	const char *oli = NULL; /* option letter list index */
	if ((opt->option = *opt->place++) == ':' || !(oli = strchr(fmt, opt->option))) { /* option letter okay? */
		 /* if the user didn't specify '-' as an option, assume it means -1.  */
		if (opt->option == '-')
			return -1;
		if (!*opt->place)
			opt->index++;
		if (opt->error && *fmt != ':')
			(void)fprintf(stderr, "illegal option -- %c\n", opt->option);
		return BADCH_E;
	}

	if (*++oli != ':') { /* don't need argument */
		opt->arg = NULL;
		if (!*opt->place)
			opt->index++;
	} else {  /* need an argument */
		if (*opt->place) { /* no white space */
			opt->arg = opt->place;
		} else if (argc <= ++opt->index) { /* no arg */
			opt->place = "";
			if (*fmt == ':')
				return BADARG_E;
			if (opt->error)
				(void)fprintf(stderr, "option requires an argument -- %c\n", opt->option);
			return BADCH_E;
		} else	{ /* white space */
			opt->arg = argv[opt->index];
		}
		opt->place = "";
		opt->index++;
	}
	return opt->option; /* dump back option letter */
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

static cdb_word_t cdb_read_cb(void *file, void *buf, size_t length) {
	assert(file);
	assert(buf);
	return fread(buf, 1, length, (FILE*)file);
}

static cdb_word_t cdb_write_cb(void *file, void *buf, size_t length) {
	assert(file);
	assert(buf);
	return fwrite(buf, 1, length, (FILE*)file);
}

static int cdb_seek_cb(void *file, long offset) {
	assert(file);
	return fseek((FILE*)file, offset, SEEK_SET);
}

static void *cdb_open_cb(const char *name, int mode) {
	assert(name);
	assert(mode == CDB_RO_MODE || mode == CDB_RW_MODE);
	const char *mode_string = mode == CDB_RW_MODE ? "wb+" : "rb";
	return fopen(name, mode_string);
}

static int cdb_close_cb(void *file) {
	assert(file);
	return fclose((FILE*)file);
}

static int cdb_flush_cb(void *file) {
	assert(file);
	return fflush((FILE*)file);
}

static int cdb_print(cdb_t *cdb, const cdb_file_pos_t *fp, FILE *output) {
	assert(cdb);
	assert(fp);
	assert(output);
	if (cdb_seek(cdb, fp->position) < 0)
		return -1;
	char buf[IO_BUFFER_SIZE];
	const size_t length = fp->length;
	for (size_t i = 0; i < length; i += sizeof buf) {
		const size_t l = length - i;
		assert(l <= sizeof buf);
		if (cdb_read(cdb, buf, MIN(sizeof buf, l)) < 0)
			return -1;
		if (fwrite(buf, 1, l, output) != l)
			return -1;
	}
	return 0;
}

static inline void reverse(char * const r, const size_t length) {
	const size_t last = length - 1;
	for (size_t i = 0; i < length / 2ul; i++) {
		const size_t t = r[i];
		r[i] = r[last - i];
		r[last - i] = t;
	}
}

static unsigned num_to_str(char b[64], cdb_word_t u) {
	unsigned i = 0;
	do {
		const cdb_word_t base = 10; /* bases 2-10 allowed */
		const cdb_word_t q = u % base;
		const cdb_word_t r = u / base;
		b[i++] = q + '0';
		u = r;
	} while (u);
	b[i] = '\0';
	reverse(b, i);
	return i;
}

static int cdb_dump(cdb_t *cdb, const cdb_file_pos_t *key, const cdb_file_pos_t *value, void *param) {
	assert(cdb);
	assert(key);
	assert(value);
	assert(param);
	FILE *output = param;
	char kstr[64+1], vstr[64+2];
	kstr[0] = '+';
	const unsigned kl = num_to_str(kstr + 1, key->length) + 1;
	vstr[0] = ',';
	const unsigned nl = num_to_str(vstr + 1, value->length) + 1;
	if (fwrite(kstr, 1, kl, output) != kl)
		return -1;
	vstr[nl]     = ':';
	vstr[nl + 1] = '\0';
	if (fwrite(vstr, 1, nl + 1, output) != (nl + 1))
		return -1;
	if (cdb_print(cdb, key, output) < 0)
		return -1;
	if (fwrite("->", 1, 2, output) != 2)
		return -1;
	if (cdb_print(cdb, value, output) < 0)
		return -1;
	return fputc('\n', output);
}

static int cdb_dump_keys(cdb_t *cdb, const cdb_file_pos_t *key, const cdb_file_pos_t *value, void *param) {
	assert(cdb);
	assert(key);
	assert(value);
	assert(param);
	FILE *output = param;
	char kstr[64+2];
	kstr[0] = '+';
	const unsigned kl = num_to_str(kstr + 1, key->length) + 1;
	kstr[kl]     = ':'; 
	kstr[kl + 1] = '\0';
	if (fwrite(kstr, 1, kl + 1, output) != (kl + 1))
		return -1;
	if (cdb_print(cdb, key, output) < 0)
		return -1;
	return fputc('\n', output);
}

static int str_to_num(const char *s, cdb_word_t *out) {
	assert(s);
	cdb_word_t result = 0;
	int ch = s[0];
	*out = 0;
	if (!ch)
		return -1;
	for (size_t j = 0; j < 64 && (ch = s[j]); j++) {
		const int digit = ch - '0';
		if (digit < 0 || digit > 9)
			return -1;
		result = digit + (result * (cdb_word_t)10ul);
	}
	if (ch)
		return -1;
	*out = result;
	return 0;
}

static int scan(FILE *input, cdb_word_t *out, int delim) {
	assert(input);
	char b[64];
	size_t i = 0;
	int ch = 0;
	for (i = 0; i < sizeof (b) && (EOF != (ch = fgetc(input))) && isdigit(ch); i++)
		b[i] = ch;
	if (i == sizeof(b))
		return -1;
	b[i] = '\0';
	if (delim == 0) {
		if (ungetc(ch, input) < 0)
			return -1;
	} else if (ch != delim) {
		return -1;
	}
	return str_to_num(b, out);
}

static int cdb_create(cdb_t *cdb, FILE *input) {
	assert(cdb);
	assert(input);

	char ibuf[BUFSIZ];
	if (setvbuf(input, ibuf, _IOFBF, sizeof ibuf) < 0)
		return -1;

	int r = 0;
	size_t kmlen = IO_BUFFER_SIZE, vmlen = IO_BUFFER_SIZE;
	char *key = malloc(kmlen);
	char *value = malloc(vmlen);
	if (!key || !value)
		goto fail;

	for (;;) {
		cdb_word_t klen = 0, vlen = 0;
		char sep[2] = { 0 };
		const int first = fgetc(input);
		if (first == EOF)
			goto end;
		if (isspace(first))
			continue;
		if (first != '+')
			goto fail;
		if (scan(input, &klen, ',') < 0)
			goto fail;
		if (scan(input, &vlen, ':') < 0)
			goto fail;
		if (kmlen < klen) {
			char *t = realloc(key, klen);
			if (!t)
				goto fail;
			kmlen = klen;
			key = t;
		}

		if (vmlen < vlen) {
			char *t = realloc(value, vlen);
			if (!t)
				goto fail;
			vmlen = vlen;
			value = t;
		}

		if (fread(key, 1, klen, input) != klen)
			goto fail;

		if (fread(sep, 1, sizeof sep, input) != sizeof sep)
			goto fail;

		if (sep[0] != '-' || sep[1] != '>')
			goto fail;

		if (fread(value, 1, vlen, input) != vlen)
			goto fail;

		const cdb_buffer_t kb = { .length = klen, .buffer = key };
		const cdb_buffer_t vb = { .length = vlen, .buffer = value };

		if (cdb_add(cdb, &kb, &vb) < 0) {
			(void)fprintf(stderr, "cdb file add failed\n");
			//remove(file);
			goto fail;
		}
		const int ch1 = fgetc(input);
		if (ch1 == '\n')
			continue;
		if (ch1 == EOF)
			goto end;
		if (ch1 != '\r')
			goto fail;
		if ('\n' != fgetc(input)) {
		}
	}
fail:
	r = -1;
end:
	free(key);
	free(value);
	return r;
}

static int cdb_stats(cdb_t *cdb, const cdb_file_pos_t *key, const cdb_file_pos_t *value, void *param) {
	assert(cdb);
	assert(key);
	assert(value);
	assert(param);
	cdb_statistics_t *cs = param;
	cs->records++;
	cs->total_key_length   += key->length;
	cs->total_value_length += value->length;
	cs->min_key_length      = MIN(cs->min_key_length,   key->length);
	cs->min_value_length    = MIN(cs->min_value_length, value->length);
	cs->max_key_length      = MAX(cs->max_key_length,   key->length);
	cs->max_value_length    = MAX(cs->max_value_length, value->length);
	return 0;
}

static int cdb_stats_print(cdb_t *cdb, FILE *output, int verbose) {
	assert(cdb);
	assert(output);
	unsigned long distances[DISTMAX] = { 0 };
	unsigned long entries = 0, occupied = 0, collisions = 0, hmin = ULONG_MAX, hmax = 0;
	double avg_key_length = 0, avg_value_length = 0, avg_hash_length = 0;
	cdb_statistics_t s = {
		.records          = 0,
		.min_key_length   = ULONG_MAX,
		.min_value_length = ULONG_MAX,
	};

	if (cdb_foreach(cdb, cdb_stats, &s) < 0)
		return -1;

	if (verbose)
		if (fputs("Initial hash table: \n", output) < 0)
			return -1;

	for (size_t i = 0; i < 256; i++) {
		if (cdb_seek(cdb, i * (2u * sizeof (cdb_word_t))) < 0)
			return -1;
		cdb_word_t pos = 0, num = 0;
		if (cdb_read_word_pair(cdb, &pos, &num) < 0)
			return -1;
		if (verbose) {
			if ((i % 4) == 0)
				if (fprintf(output, "\n%3d:\t", (int)i) < 0)
					return -1;
			if (fprintf(output, "$%4lx %3ld, ", (long)pos, (long)num) < 0)
				return -1;
		}

		collisions += num > 2ul;
		entries    += num;
		occupied   += num != 0;
		hmax        = MAX(num, hmax);
		if (num)
			hmin = MIN(num, hmin);
		if (cdb_seek(cdb, pos) < 0)
			return -1;
		for (size_t i = 0; i < num; i++) {
			cdb_word_t h = 0, p = 0;
			if (cdb_read_word_pair(cdb, &h, &p) < 0)
				return -1;
			if (!p)
				continue;
			h = (h >> 8) % num;
			if (h == i) {
				h = 0;
			} else {
				h = h < i ? i - h : num - h + i;
				h = MIN(h, DISTMAX - 1ul);
			}
			distances[h]++;
		}
	}

	if (verbose)
		if (fputs("\n\n", output) < 0)
			return -1;

	if (s.records == 0) {
		s.min_key_length = 0;
		s.min_value_length = 0;
		hmin = 0;
	} else {
		avg_key_length   = (double)s.total_key_length / (double) s.records;
		avg_value_length = (double)s.total_value_length / (double) s.records;
		avg_hash_length  = (double)entries / (double)occupied;
	}

	if (fprintf(output, "records:\t\t\t%lu\n", s.records) < 0)
		return -1;
	if (fprintf(output, "key   min/max/avg/bytes:\t%lu/%lu/%g/%lu\n",
		s.min_key_length, s.max_key_length, avg_key_length, s.total_key_length) < 0)
		return -1;
	if (fprintf(output, "value min/max/avg/bytes:\t%lu/%lu/%g/%lu\n",
		s.min_value_length, s.max_value_length, avg_value_length, s.total_value_length) < 0)
		return -1;
	if (fprintf(output, "top hash table used/entries/collisions:\t%lu/%lu/%lu\n", occupied, entries, collisions) < 0)
		return -1;
	if (fprintf(output, "hash tables min/avg/max:\t%lu/%g/%lu\n", hmin, avg_hash_length, hmax) < 0)
		return -1;
	if (fprintf(output, "hash tables collisions/buckets:\t%lu/%lu\n", s.records - distances[0], entries) < 0)
		return -1;
	if (fputs("hash table distances:\n", output) < 0)
		return -1;

	for (size_t i = 0; i < DISTMAX; i++) {
		const double pct = s.records ? ((double)distances[i] / (double)s.records) * 100.0 : 0.0;
		if (fprintf(output, "\td%u%s %4lu %5.2g%%\n", (unsigned)i, i == DISTMAX - 1ul ? "+:" : ": ", distances[i], pct) < 0)
			return -1;
	}
	return 0;
}

static int cdb_query(cdb_t *cdb, char *key, int record, FILE *output) {
	assert(cdb);
	assert(key);
	assert(output);
	const cdb_buffer_t kb = { .length = strlen(key), .buffer = key };
	cdb_file_pos_t vp = { 0, 0 };
	const int gr = cdb_get_record(cdb, &kb, &vp, record);
	if (gr < 0)
		return -1;
	if (gr > 0) /* found */
		return cdb_print(cdb, &vp, output) < 0 ? -1 : 0;
	return 2; /* not found */
}

static int cdb_null_cb(cdb_t *cdb, const cdb_file_pos_t *key, const cdb_file_pos_t *value, void *param) {
	UNUSED(cdb);
	UNUSED(key);
	UNUSED(value);
	UNUSED(param);
	return 0;
}

static int help(FILE *output, const char *arg0) {
	assert(output);
	assert(arg0);
	unsigned long version = 0;
	if (cdb_get_version(&version) < 0)
		info("version not set - built incorrectly");
	const unsigned q = (version >> 24) & 0xff;
	const unsigned x = (version >> 16) & 0xff;
	const unsigned y = (version >>  8) & 0xff;
	const unsigned z = (version >>  0) & 0xff;
	static const char *usage = "\
Usage   : %s -hv *OR* -[rcdkstVT] file.cdb *OR* -q file.cdb key [record#]\n\
Program : Constant Database Driver (clone of https://cr.yp.to/cdb.html)\n\
Author  : Richard James Howe\n\
Email   : howe.r.j.89@gmail.com\n\
Repo    : <https://github.com/howerj/cdb>\n\
License : The Unlicense\n\
Version : %u.%u.%u\n\
Options : 0x%x\n\
Size    : %d\n\
Notes   : See manual pages or project website for more information.\n\n\
Options :\n\n\
\t-h          : print this help message and exit successfully\n\
\t-v          : increase verbosity level\n\
\t-c file.cdb : create a new database reading keys from stdin\n\
\t-d file.cdb : dump entire database\n\
\t-k file.cdb : dump all keys (there may be duplicates)\n\
\t-s file.cdb : calculate database statistics\n\
\t-t file.cdb : run internal tests generating a test file\n\
\t-T temp.cdb : name of temporary file to use\n\
\t-V file.cdb : validate database\n\
\t-q file.cdb key #? : run query for key with optional record number\n\n\
In create mode the key input format is:\n\n\
\t+key-length,value-length:key->value\n\n\
An example:\n\n\
\t+5,5:hello->world\n\n\
Queries are in a similar format:\n\n\
\t+key-length:key\n\n\
Binary key/values are allowed, as are duplicate and empty keys/values.\n\
Returns values of 0 indicate success/found, 2 not found, and anything else\n\
is an error.\n\
";
	return fprintf(output, usage, arg0, x, y, z, q,(int)(sizeof (cdb_word_t) * CHAR_BIT));
}

int main(int argc, char **argv) {
	enum { QUERY, DUMP, CREATE, STATS, KEYS, VALIDATE };
	const char *file = NULL;
	char *tmp = NULL;
	int mode = VALIDATE, creating = 0;

	binary(stdin);
	binary(stdout);
	binary(stderr);

	static const cdb_callbacks_t ops = {
		.allocator = cdb_allocator_cb,
		.read      = cdb_read_cb,
		.write     = cdb_write_cb,
		.seek      = cdb_seek_cb,
		.open      = cdb_open_cb,
		.close     = cdb_close_cb,
		.flush     = cdb_flush_cb,
	};

	cdb_getopt_t opt = { .init = 0 };
	for (int ch = 0; (ch = cdb_getopt(&opt, argc, argv, "hvt:c:d:k:s:q:V:T:")) != -1; ) {
		switch (ch) {
		case 'h': return help(stdout, argv[0]), 0;
		case 't': return -cdb_tests(&ops, NULL, opt.arg);
		case 'v': verbose++;                       break;
		case 'c': file = opt.arg; mode = CREATE;   break;
		case 'd': file = opt.arg; mode = DUMP;     break;
		case 'k': file = opt.arg; mode = KEYS;     break;
		case 's': file = opt.arg; mode = STATS;    break;
		case 'q': file = opt.arg; mode = QUERY;    break;
		case 'V': file = opt.arg; mode = VALIDATE; break;
		case 'T': tmp = opt.arg;                   break;
		default: help(stderr, argv[0]); return 1;
		}
	}

	if (!file)
		return help(stderr, argv[0]), 1;

	creating = mode == CREATE;

	cdb_t *cdb = NULL;
	errno = 0;
	const char *name = creating && tmp ? tmp : file;
	info("opening '%s' for %s", name, creating ? "writing" : "reading");

	if (cdb_open(&cdb, &ops, NULL, creating, name) < 0) {
		const char *stre = strerror(errno);
		const char *mstr = creating ? "create" : "read";
		die("opening file '%s' in %s mode failed: %s", name, mstr, stre);
	}

	int r = 0;
	switch (mode) {
	case CREATE:   r = cdb_create(cdb, stdin);                                                       break;
	case DUMP:     r = cdb_foreach(cdb, cdb_dump,      stdout); if (fputc('\n', stdout) < 0) r = -1; break;
	case KEYS:     r = cdb_foreach(cdb, cdb_dump_keys, stdout); if (fputc('\n', stdout) < 0) r = -1; break;
	case STATS:    r = cdb_stats_print(cdb, stdout, 0);                                              break;
	case VALIDATE: r = cdb_foreach(cdb, cdb_null_cb, NULL);                                          break;
	case QUERY: {
		if (opt.index >= argc)
			die("-q opt requires key (and optional record number)");
		char *key = argv[opt.index++];
		r = cdb_query(cdb, key, opt.index < argc ? atoi(argv[opt.index++]) : 0, stdout);
		break;
	}
	default:
		die("unimplemented mode: %d", mode);
	}

	const int cdbe = cdb_get_error(cdb);
	if (cdb_close(cdb) < 0)
		die("Close/Finalize failed");
	if (cdbe < 0)
		die("CDB internal error: %d", cdbe);

	if (creating && tmp) {
		info("renaming temporary file");
		(void)remove(file);
		if (rename(tmp, file) < 0)
			die("rename from '%s' to '%s' failed: %s", tmp, file, strerror(errno));
	}
	return r;
}

