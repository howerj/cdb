#include "cdb.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNUSED(X) ((void)(X))

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

void *cdb_open_cb(const char *name, const char *mode) {
	assert(name);
	assert(mode);
	return fopen(name, mode);
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

static int help(FILE *output, const char *arg0) {
	assert(output);
	assert(arg0);
	static const char *fmt = "\
usage: %s -[ht]\n\
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
\n\
This program returns zero on success and non zero on failure, errors are\n\
printed out to stderr.\n\n\
";
	return fprintf(output, fmt, arg0);
}

int main(int argc, char **argv) {
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
		.file  = NULL,
	};

	cdb_getopt_t opt = { .init = 0 };
	int ch = 0;
	while ((ch = cdb_getopt(&opt, argc, argv, "ht")) != -1) {
		switch (ch) {
		case 'h': help(stdout, argv[0]); return 0;
		case 't': return cdb_tests(&ops, &allocator);
		default: help(stderr, argv[0]); return -1;
		}
	}

	return 0;
}

