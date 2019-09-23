#include "cdb.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNUSED(X) ((void)(X))
#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))

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
		/*if (opt->error && *fmt != ':')
			(void)fprintf(stderr, "illegal option -- %c\n", opt->option);*/
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
			/*if (opt->error)
				(void)fprintf(stderr, "option requires an argument -- %c\n", opt->option);*/
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
	switch (whence) {
	case CDB_SEEK_START:   whence = SEEK_SET; break;
	case CDB_SEEK_END:     whence = SEEK_END; break;
	case CDB_SEEK_CURRENT: whence = SEEK_CUR; break;
	default:
		return -1;
	}

	return fseek((FILE*)file, offset, whence);
}

void *cdb_open_cb(const char *name, int mode) {
	assert(name);
	assert(mode == CDB_RO_MODE || mode == CDB_RW_MODE);
	const char *mode_string = mode == CDB_RW_MODE ? "wb+" : "rb";
	return fopen(name, mode_string);
}

long cdb_close_cb(void *file) {
	assert(file);
	return fclose((FILE*)file);
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

static void die_io(void) {
	die("i/o error");
}

static int cdb_print(cdb_t *cdb, const cdb_file_pos_t *fp, FILE *input, FILE *output) {
	assert(cdb);
	assert(fp);
	assert(input);
	assert(output);
	if (fseek(input, fp->position, SEEK_SET) < 0)
		return -1;
	char buf[256] = { 0 };
	const size_t length = fp->length;
	for (size_t i = 0; i < length; i += sizeof buf) {
		const size_t r = fread(buf, 1, MIN(sizeof buf, length - i), input);
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
	FILE *input  = cdb_get_file(cdb);
	FILE *output = param;
	assert(input);
	if (fprintf(output, "+%lu,%lu:", key->length, value->length) < 0)
		return -1;
	if (cdb_print(cdb, key, input, output) < 0)
		return -1;
	if (fputs("->", output) < 0)
		return -1;
	if (cdb_print(cdb, value, input, output) < 0)
		return -1;
	return fputc('\n', output);
}

static int cdb_dump_keys(cdb_t *cdb, const cdb_file_pos_t *key, const cdb_file_pos_t *value, void *param) {
	assert(cdb);
	assert(key);
	assert(value);
	assert(param);
	FILE *input  = cdb_get_file(cdb);
	FILE *output = param;
	assert(input);
	if (fprintf(output, "+%lu:", key->length) < 0)
		return -1;
	if (cdb_print(cdb, key, input, output) < 0)
		return -1;
	return fputc('\n', output);
}

static int cdb_normal(cdb_t *cdb, FILE *db, FILE *input, FILE *output, int create) {
	assert(cdb);
	assert(input);
	assert(db);
	assert(output);
	for (;;) {
		char key[256] = { 0 }, value[256] = { 0 };

		if (fputs("> ", output) < 0)
			die_io();
		if (fflush(output) < 0)
			die_io();

		if (create) {
			if (fscanf(input, "%256s,%256s", key, value) != 2)
				break;
		} else {
			if (fscanf(input, "%256s", key) != 1)
				break;
		}
		
		const cdb_buffer_t kb = { .length = strlen(key), .buffer = key };
		if (create) {
			const cdb_buffer_t vb = { .length = strlen(value), .buffer = value };
			if (cdb_add(cdb, &kb, &vb) < 0) {
				(void)fprintf(stderr, "cdb file add failed\n");
				//remove(file);
				return 1;
			}
		} else {
			cdb_file_pos_t vp = { 0, 0 };
			const int r = cdb_get(cdb, &kb, &vp);
			if (r < 0) {
				die("cdb get error");
			} else if (r == 0) {
				if (fputs("?\n", output) < 0)
					die_io();
			} else {
				if (cdb_read_cb(db, value, MIN(vp.length, sizeof value)) < 0)
					return -1;
				if (fprintf(output, "+%lu,%lu:%s,", strlen(key), vp.length, key) < 0)
					return -1;
				if (fputs("->", output) < 0)
					return -1;
				if (cdb_print(cdb, &vp, cdb_get_file(cdb), output) < 0)
					return -1;
				if (fputc('\n', output) < 0)
					return -1;
			}
		}
	}
	return 0;
}

static int help(FILE *output, const char *arg0) {
	assert(output);
	assert(arg0);
	static const char *fmt = "\
usage: %s -[htcdk] file.cdb\n\
CDB - Constant Database Test Driver\n\n\
Author:  Richard James Howe\n\
Email:   howe.r.j.89@gmail.com\n\
Repo:    https://github.com/howerj/cdb\n\
License: The Unlicense\n\n\
This is a reimplementation of the CDB program from DJB, which is described\n\
here at <https://cr.yp.to/cdb.html>. The program is built around a portable\n\
library and provides an interface for manipulating CDB files. The program\n\
aims to be compatible with the original file format, but not with the original\n\
command line interface.\n\n\
Options:\n\n\
\t-h\tprint out this help message and exit successfully.\n\
\t-t\trun internal tests, exit with zero on a pass\n\
\t-c\trun in create mode\n\
\t-d\tdump the database\n\
\t-k\tdump the keys in the database\n\
\n\
This program returns zero on success and non zero on failure, errors are\n\
printed out to stderr.\n\n\
";
	return fprintf(output, fmt, arg0);
}

int main(int argc, char **argv) {
	enum { READ, DUMP, CREATE, STATS, KEYS };
	const char *file = NULL;
	int mode = READ;
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
	};

	cdb_getopt_t opt = { .init = 0 };
	int ch = 0;
	while ((ch = cdb_getopt(&opt, argc, argv, "htcdk")) != -1) {
		switch (ch) {
		case 'h': help(stdout, argv[0]); return 0;
		case 't': return cdb_tests(&ops, &allocator);
		case 'c': mode = CREATE; break;
		case 'd': mode = DUMP; break;
		case 'k': mode = KEYS; break;
		default: help(stderr, argv[0]); return -1;
		}
	}

	if (opt.index >= argc)
		die("No database supplied, consult help (with -h option)");
	file = argv[opt.index++];

	cdb_t *cdb = NULL;
	errno = 0;
	if (cdb_open(&cdb, &ops, &allocator, mode == CREATE, file) < 0)
		die("opening file '%s' in %s mode failed: %s", file, mode == CREATE ? "create" : "read", strerror(errno));

	int r = 0;
	switch (mode) {
	case CREATE: r = cdb_normal(cdb, cdb_get_file(cdb), stdin, stdout, 1); break;
	case READ:   r = cdb_normal(cdb, cdb_get_file(cdb), stdin, stdout, 0); break;
	case DUMP:   r = cdb_foreach(cdb, cdb_dump, stdout ); break;
	case KEYS:   r = cdb_foreach(cdb, cdb_dump_keys, stdout ); break;
	default:
		die("unimplemented mode: %d", mode);
	}

	if (cdb_close(cdb) < 0)
		die("Close/Finalize failed");

	return r;
}

