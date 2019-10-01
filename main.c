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

#define UNUSED(X) ((void)(X))
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))
#define IO_BUFFER_SIZE (4096)

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
} cdb_getopt_t;   /* getopt clone; with a few modifications */

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

static void die(const char *fmt, ...) {
	assert(fmt);
	va_list arg;
	va_start(arg, fmt);
	vfprintf(stderr, fmt, arg);
	va_end(arg);
	fputc('\n', stderr);
	exit(EXIT_FAILURE);
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

static int cdb_seek_cb(void *file, long offset, long whence) {
	assert(file);
	switch (whence) {
	case CDB_SEEK_START:   whence = SEEK_SET; break;
	case CDB_SEEK_END:     whence = SEEK_END; break;
	case CDB_SEEK_CURRENT: whence = SEEK_CUR; break;
	default:
		return -1;
	}
	return fseek((FILE*)file, offset, whence);
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

static int cdb_print(cdb_t *cdb, const cdb_file_pos_t *fp, FILE *output) {
	assert(cdb);
	assert(fp);
	assert(output);
	if (cdb_seek(cdb, fp->position, CDB_SEEK_START) < 0)
		return -1;
	char buf[IO_BUFFER_SIZE] = { 0 };
	const size_t length = fp->length;
	for (size_t i = 0; i < length; i += sizeof buf) {
		const size_t r = cdb_read(cdb, buf, MIN(sizeof buf, length - i));
		assert(r <= sizeof buf);
		if (fwrite(buf, 1, r, output) != r)
			return -1;
	}
	return 0;
}

static int cdb_dump(cdb_t *cdb, const cdb_file_pos_t *key, const cdb_file_pos_t *value, void *param) {
	assert(cdb);
	assert(key);
	assert(value);
	assert(param);
	FILE *output = param;
	if (fprintf(output, "+%lu,%lu:", (unsigned long)key->length, (unsigned long)value->length) < 0)
		return -1;
	if (cdb_print(cdb, key, output) < 0)
		return -1;
	if (fputs("->", output) < 0)
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
	if (fprintf(output, "+%lu:", (unsigned long)key->length) < 0)
		return -1;
	if (cdb_print(cdb, key, output) < 0)
		return -1;
	return fputc('\n', output);
}

static int cdb_create(cdb_t *cdb, FILE *input) {
	assert(cdb);
	assert(input);
	int r = 0;
	size_t kmlen = IO_BUFFER_SIZE, vmlen = IO_BUFFER_SIZE;
	char *key = malloc(kmlen);
	char *value = malloc(vmlen);
	if (!key || !value)
		goto fail;
	for (;;) {
		unsigned long klen = 0, vlen = 0;
		char sep[2] = { 0 };
		if (fscanf(input, "+%lu,%lu", &klen, &vlen) != 2)
			goto end;
		if (fgetc(input) != ':')
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

#define DISTMAX (10)

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

	if (verbose) {
		if (fputs("Initial hash table: \n", output) < 0)
			return -1;
	}

	for (size_t i = 0; i < 256; i++) {
		if (cdb_seek(cdb, i * (2u * sizeof (cdb_word_t)), CDB_SEEK_START) < 0)
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
		entries += num;
		occupied += num != 0;
		hmax = MAX(num, hmax);
		if (num)
			hmin = MIN(num, hmin);
		if (cdb_seek(cdb, pos, CDB_SEEK_START) < 0)
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
				h = MIN(h, DISTMAX - 1);
			}
			distances[h]++;
		}
	}
	if (verbose) {
		if (fputs("\n\n", output) < 0)
			return -1;
	}
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
	if (fprintf(output, "top hash table used/collisions:\t%lu/%lu/%lu\n", occupied, entries, collisions) < 0)
		return -1;
	if (fprintf(output, "hash tables min/avg/max:\t%lu/%g/%lu\n", hmin, avg_hash_length, hmax) < 0)
		return -1;
	if (fprintf(output, "hash tables collisions/buckets:\t%lu/%lu\n", s.records - distances[0], entries) < 0)
		return -1;
	if (fputs("hash table distances:\n", output) < 0)
		return -1;
	for (size_t i = 0; i < DISTMAX; i++) {
		const double pct = s.records ? ((double)distances[i] / (double)s.records) * 100.0 : 0.0;
		if (fprintf(output, "\td%u%s %4lu %5.2g%%\n", (unsigned)i, i == DISTMAX - 1 ? "+:" : ": ", distances[i], pct) < 0)
			return -1;
	}
	return 0;
}

static int cdb_prompt(FILE *output, const char *prompt) {
	assert(output);
	if (fputs(prompt, output) < 0)
		return -1;
	if (fflush(output) < 0)
		return -1;
	return 0;
}

static int cdb_query_prompt(cdb_t *cdb, FILE *input, FILE *output, const char *prompt) {
	assert(cdb);
	assert(input);
	assert(output);
	assert(prompt);
	int r = 0;
	size_t kmlen = IO_BUFFER_SIZE;
	char *key = malloc(kmlen);
	if (!key)
		goto fail;
	if (cdb_prompt(output, prompt) < 0)
		goto fail;
	for (;;) {
		unsigned long klen = 0;
		const int ch0 = fgetc(input);
		switch (ch0) {
		case 'q': case EOF: goto end;
		case ' ': case '\t': case '\r': continue;
		case '\n': goto prompt;
		case 's': 
			if (cdb_stats_print(cdb, output, 0) < 0)
				goto fail;
			continue;
		case 'k': 
			if (cdb_foreach(cdb, cdb_dump_keys, output) < 0)
				goto fail;
			continue;
		case 'd': 
			if (cdb_foreach(cdb, cdb_dump, output) < 0)
				goto fail;
			continue;
		}

		if (ch0 != '+')
			goto wrong;
		if (fscanf(input, "%lu", &klen) != 1)
			goto wrong;

		int ch1 = fgetc(input);
		unsigned long record = 0;
		if (ch1 == '#') {
			if (fscanf(input, "%lu", &record) != 1)
				goto wrong;
			ch1 = fgetc(input);
		} 
		if (':' != ch1)
			goto wrong;

		if (kmlen < klen) {
			char *t = realloc(key, klen);
			if (!t)
				goto fail;
			key = t;
		}

		if (klen != fread(key, 1, klen, input))
			goto wrong;

		const int ch2 = fgetc(input);
		if (ch2 != '\n' && ch2 != EOF)
			if (ungetc(ch2, input) < 0)
				goto wrong;
		
		const cdb_buffer_t kb = { .length = klen, .buffer = key };
		cdb_file_pos_t vp = { 0, 0 };
		const int g = cdb_get_record(cdb, &kb, &vp, record);
		if (g < 0) {
			goto fail;
		} else if (g == 0) {
			goto wrong;
		} else {
			if (fprintf(output, "%lu:", (unsigned long)vp.length) < 0)
				goto fail;
			if (cdb_print(cdb, &vp, output) < 0)
				goto fail;
			if (fputc('\n', output) < 0)
				goto fail;
		}
		continue;
	wrong:
		if (cdb_prompt(output, "?\n") < 0)
			goto fail;
		continue;
	prompt:
		if (cdb_prompt(output, prompt) < 0)
			return -1;
	}
fail:
	r = -1;
end:
	free(key);
	return r;
}

static int cdb_query(cdb_t *cdb, char *key, int record, FILE *output) {
	assert(cdb);
	assert(key);
	assert(output);
	int r = 0;
	const cdb_buffer_t kb = { .length = strlen(key), .buffer = key };
	cdb_file_pos_t vp = { 0, 0 };
	const int gr = cdb_get_record(cdb, &kb, &vp, record);
	if (gr < 0) {
		r = -1;
	} else {
		if (gr > 0)
			r = cdb_print(cdb, &vp, output);
		else
			r = 2; /* not found */
	}
	return r;
}

static int cdb_null_cb(cdb_t *cdb, const cdb_file_pos_t *key, const cdb_file_pos_t *value, void *param) {
	UNUSED(cdb);
	UNUSED(key);
	UNUSED(value);
	UNUSED(param);
	return 0;
}

static int cdb_validate(cdb_t *cdb) {
	assert(cdb);
	return cdb_foreach(cdb, cdb_null_cb, NULL);
}

static int help(FILE *output, const char *arg0) {
	assert(output);
	assert(arg0);
	unsigned long version = cdb_version();
	unsigned x = (version >>  0) & 0xff;
	unsigned y = (version >>  8) & 0xff;
	unsigned z = (version >> 16) & 0xff;
	static const char *usage = "\
Usage:   %s -h *OR* -[rcdkstV] file.cdb *OR* -q file.cdb key [record#]\n\
Program: Constant Database Driver (clone of https://cr.yp.to/cdb.html)\n\
Author:  Richard James Howe\n\
Email:   howe.r.j.89@gmail.com\n\
Repo:    https://github.com/howerj/cdb\n\
License: The Unlicense\n\
Version: %u.%u.%u\n\
Size:    %d\n\
Notes:   See manual pages or project website for more information.\n\n";
	return fprintf(output, usage, arg0, x, y, z, (int)(sizeof (cdb_word_t) * CHAR_BIT));
}

int main(int argc, char **argv) {
	enum { READ, QUERY, DUMP, CREATE, STATS, KEYS, VALIDATE };
	const char *file = NULL, *prompt = "> ";
	int mode = READ;

	binary(stdin);
	binary(stdout);
	binary(stderr);

	cdb_allocator_t allocator = {
		.malloc  = cdb_malloc_cb,
		.realloc = cdb_realloc_cb,
		.free    = cdb_free_cb,
		.arena   = NULL,
	};

	cdb_file_operators_t ops = {
		.read  = cdb_read_cb,
		.write = cdb_write_cb,
		.seek  = cdb_seek_cb,
		.open  = cdb_open_cb,
		.close = cdb_close_cb,
		.flush = cdb_flush_cb,
	};

	cdb_getopt_t opt = { .init = 0 };
	int ch = 0;
	while ((ch = cdb_getopt(&opt, argc, argv, "hr:t:c:d:k:s:q:V:p:")) != -1) {
		switch (ch) {
		case 'h': return help(stdout, argv[0]), 0;
		case 't': return -cdb_tests(&ops, &allocator, opt.arg);
		case 'r': file = opt.arg; mode = READ;   break;
		case 'c': file = opt.arg; mode = CREATE;   break;
		case 'd': file = opt.arg; mode = DUMP;     break;
		case 'k': file = opt.arg; mode = KEYS;     break;
		case 's': file = opt.arg; mode = STATS;    break;
		case 'q': file = opt.arg; mode = QUERY;    break;
		case 'V': file = opt.arg; mode = VALIDATE; break;
		case 'p': prompt = opt.arg; break;
		default: help(stderr, argv[0]); return 1;
		}
	}

	if (!file)
		return help(stderr, argv[0]), 1;

	cdb_t *cdb = NULL;
	errno = 0;
	if (cdb_open(&cdb, &ops, &allocator, mode == CREATE, file) < 0)
		die("opening file '%s' in %s mode failed: %s", file, mode == CREATE ? "create" : "read", strerror(errno));

	int r = 0;
	switch (mode) {
	case CREATE:   r = cdb_create(cdb, stdin);                       break;
	case READ:     r = cdb_query_prompt(cdb, stdin, stdout, prompt); break;
	case DUMP:     r = cdb_foreach(cdb, cdb_dump, stdout);           break;
	case KEYS:     r = cdb_foreach(cdb, cdb_dump_keys, stdout);      break;
	case STATS:    r = cdb_stats_print(cdb, stdout, 0);              break;
	case VALIDATE: r = cdb_validate(cdb);                            break;
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

	if (cdb_close(cdb) < 0)
		die("Close/Finalize failed");

	return r;
}
