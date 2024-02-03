/* Program: Constant Database Driver
 * Author:  Richard James Howe
 * Email:   howe.r.j.89@gmail.com
 * License: Unlicense
 * Repo:    <https://github.com/howerj/cdb> */

#include "cdb.h"
#include "host.h"
#include "mem.h"
#include "extra.h"
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

static unsigned verbose = 0;

static void info(const char *fmt, ...) {
	assert(fmt);
	if (verbose == 0)
		return;
	FILE *out = stderr;
	va_list ap;
	va_start(ap, fmt);
	(void)vfprintf(out, fmt, ap);
	va_end(ap);
	(void)fputc('\n', out);
	(void)fflush(out);
}

static void die(const char *fmt, ...) {
	assert(fmt);
	FILE *out = stderr;
	va_list ap;
	va_start(ap, fmt);
	(void)vfprintf(out, fmt, ap);
	va_end(ap);
	(void)fputc('\n', out);
	(void)fflush(out);
	exit(EXIT_FAILURE);
}

static int cdb_print(cdb_t *cdb, const cdb_file_pos_t *fp, FILE *output) {
	assert(cdb);
	assert(fp);
	assert(output);
	if (cdb_seek(cdb, fp->position) < 0)
		return -1;
	char buf[IO_BUFFER_SIZE];
	const size_t length = fp->length;
	for (size_t i = 0; i < length; i += sizeof buf) { /* N.B. Double buffering! */
		const size_t l = length - i;
		if (l > sizeof buf)
			return -1;
		assert(l <= sizeof buf);
		if (cdb_read(cdb, buf, MIN(sizeof buf, l)) < 0)
			return -1;
		if (fwrite(buf, 1, l, output) != l)
			return -1;
	}
	return 0;
}

static unsigned cdb_number_to_string(char b[65 /* max int size in base 2, + NUL*/], cdb_word_t u, int base) {
	assert(b);
	assert(base >= 2 && base <= 10);
	unsigned i = 0;
	do {
		const cdb_word_t radix = base;
		const cdb_word_t q = u % radix;
		const cdb_word_t r = u / radix;
		b[i++] = q + '0';
		u = r;
		assert(i <= 64);
	} while (u);
	b[i] = '\0';
	cdb_reverse_char_array(b, i);
	assert(b[i] == '\0');
	return i;
}

static int cdb_dump(cdb_t *cdb, const cdb_file_pos_t *key, const cdb_file_pos_t *value, void *param) {
	assert(cdb);
	assert(key);
	assert(value);
	assert(param);
	FILE *output = param;
	char kstr[64+1], vstr[64+2]; /* NOT INITIALIZED */
	kstr[0] = '+';
	const unsigned kl = cdb_number_to_string(kstr + 1, key->length, 10) + 1;
	vstr[0] = ',';
	const unsigned nl = cdb_number_to_string(vstr + 1, value->length, 10) + 1;
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
	return fputc('\n', output) != '\n' ? -1 : 0;
}

static int cdb_dump_keys(cdb_t *cdb, const cdb_file_pos_t *key, const cdb_file_pos_t *value, void *param) {
	assert(cdb);
	assert(key);
	assert(value);
	assert(param);
	UNUSED(value);
	FILE *output = param;
	char kstr[64+2]; /* NOT INITIALIZED */
	kstr[0] = '+';
	const unsigned kl = cdb_number_to_string(kstr + 1, key->length, 10) + 1;
	kstr[kl]     = ':';
	kstr[kl + 1] = '\0';
	if (fwrite(kstr, 1, kl + 1, output) != (kl + 1))
		return -1;
	if (cdb_print(cdb, key, output) < 0)
		return -1;
	return fputc('\n', output) != '\n' ? -1 : 0;
}

static int cdb_string_to_number(const char *s, cdb_word_t *out) {
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
	char b[64]; /* NOT INITIALIZED */
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
	return cdb_string_to_number(b, out);
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
		cdb_word_t klen = 0, vlen = 0;
		char sep[2] = { 0, };
		const int first = fgetc(input);
		if (first == EOF) /* || first == '\n' {need to handle '\r' as well} */
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
			goto fail;
		}
		const int ch1 = fgetc(input);
		if (ch1 == '\n')
			continue;
		if (ch1 == EOF)
			goto end;
		if (ch1 != '\r')
			goto fail;
		if ('\n' != fgetc(input))
			goto fail;
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
	UNUSED(cdb);
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

static int cdb_stats_print(cdb_t *cdb, FILE *output, int verbose, size_t bytes) {
	assert(cdb);
	assert(output);
	unsigned long distances[DISTMAX] = { 0, };
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
		if (fputs("Initial hash table:\n", output) < 0)
			return -1;

	for (size_t i = 0; i < 256; i++) {
		if (cdb_seek(cdb, i * (2ull * bytes)) < 0)
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
		for (size_t j = 0; j < num; j++) {
			cdb_word_t h = 0, p = 0;
			if (cdb_read_word_pair(cdb, &h, &p) < 0)
				return -1;
			if (!p)
				continue;
			h = (h >> 8) % num;
			if (h == j) {
				h = 0;
			} else {
				h = h < j ? j - h : num - h + j;
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
	cdb_file_pos_t vp = { 0, 0, };
	const int gr = cdb_lookup(cdb, &kb, &vp, record);
	if (gr < 0)
		return -1;
	if (gr > 0) /* found */
		return cdb_print(cdb, &vp, output) < 0 ? -1 : 0;
	return 2; /* not found */
}

/* We should output directly to a database as well... */
static int generate(FILE *output, unsigned long records, unsigned long min, unsigned long max, unsigned long seed) {
	assert(output);
	uint64_t s[2] = { seed, 0, };
	if (max == 0)
		max = 1024;
	if (min > max)
		min = max;
	if ((max + min) > max)
		return -1;
	for (uint64_t i = 0; i < records; i++) {
		const unsigned long kl = (cdb_prng(s) % (max + min)) + min; /* adds bias but so what fight me */
		const unsigned long vl = (cdb_prng(s) % (max + min)) + min;
		if (fprintf(output, "+%lu,%lu:", kl, vl) < 0)
			return -1;
		for (unsigned long j = 0; j < kl; j++)
			if (fputc('a' + (cdb_prng(s) % 26), output) < 0)
				return -1;
		if (fputs("->", output) < 0)
			return -1;
		for (unsigned long j = 0; j < vl; j++)
			if (fputc('a' + (cdb_prng(s) % 26), output) < 0)
				return -1;
		if (fputc('\n', output) < 0)
			return -1;
	}
	if (fputc('\n', output) < 0)
		return -1;
	return 0;
}

static int hasher(FILE *input, FILE *output) { /* should really input keys in "+length:key\n" format */
	assert(input);
	assert(output);
	char line[512] = { 0, }; /* long enough for everyone right? */
	for (; fgets(line, sizeof line, input); line[0] = 0) {
		size_t l = strlen(line);
		if (l && line[l-1] == '\n')
			line[l--] = 0;
		if (fprintf(output, "0x%08lx\n", (unsigned long)cdb_hash((uint8_t*)line, l)) < 0)
			return -1;
	}
	return 0;
}

static int help(FILE *output, const char *arg0) {
	assert(output);
	assert(arg0);
	unsigned long version = 0;
	if (cdb_version(&version) < 0)
		info("version not set - built incorrectly");
	const unsigned q = (version >> 24) & 0xff;
	const unsigned x = (version >> 16) & 0xff;
	const unsigned y = (version >>  8) & 0xff;
	const unsigned z = (version >>  0) & 0xff;
	static const char *usage = "\
Usage   : %s -hv *OR* -[rcdkstVT] file.cdb *OR* -q file.cdb key [record#] *OR* -g *OR* -H\n\
Program : Constant Database Driver (clone of https://cr.yp.to/cdb.html)\n\
Author  : " CDB_AUTHOR "\n\
Email   : " CDB_EMAIL "\n\
Repo    : " CDB_REPO "\n\
License : " CDB_LICENSE "\n\
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
\t-q file.cdb key #? : run query for key with optional record number\n\
\t-b size     : database size (valid sizes = 16, 32 (default), 64)\n\
\t-o number   : specify offset into file where database begins\n\
\t-H          : hash keys and output their hash\n\
\t-g          : spit out an example database *dump* to standard out\n\
\t-m number   : set minimum length of generated record\n\
\t-M number   : set maximum length of generated record\n\
\t-R number   : set number of generated records\n\
\t-S number   : set seed for record generation\n\n\
In create mode the key input format is:\n\n\
\t+key-length,value-length:key->value\n\n\
An example:\n\n\
\t+5,5:hello->world\n\n\
Queries are in a similar format:\n\n\
\t+key-length:key\n\n\
Binary key/values are allowed, as are duplicate and empty keys/values.\n\
Returns values of 0 indicate success/found, 2 not found, and anything else\n\
indicates an error.\n\
";
	return fprintf(output, usage, arg0, x, y, z, q,(int)(sizeof (cdb_word_t) * CHAR_BIT));
}

int main(int argc, char **argv) {
	enum { QUERY, DUMP, CREATE, STATS, KEYS, VALIDATE, GENERATE, };
	const char *file = NULL;
	char *tmp = NULL;
	int mode = VALIDATE, creating = 0, memory_only = 0;
	unsigned long min = 0ul, max = 1024ul, records = 1024ul, seed = 0ul;

	binary(stdin);
	binary(stdout);
	binary(stderr);

	char ibuf[BUFSIZ], obuf[BUFSIZ]; /* NOT INITIALIZED */
	if (setvbuf(stdin, ibuf, _IOFBF, sizeof ibuf) < 0)
		return -1;
	if (setvbuf(stdout, obuf, _IOFBF, sizeof obuf) < 0)
		return -1;

	cdb_callbacks_t ops = cdb_host_options;

	cdb_getopt_t opt = { .init = 0, .error = stderr, };
	for (int ch = 0; (ch = cdb_getopt(&opt, argc, argv, "hHgvt:c:d:k:s:q:V:b#T:m#M#R#S#o#G:")) != -1; ) {
		switch (ch) {
		case 'h': return help(stdout, argv[0]), 0;
		case 'H': return hasher(stdin, stdout);
		case 't': return -cdb_tests(&ops, opt.arg);
		case 'v': verbose++;                       break;
		case 'c': file = opt.arg; mode = CREATE;   break;
		case 'd': file = opt.arg; mode = DUMP;     break;
		case 'k': file = opt.arg; mode = KEYS;     break;
		case 's': file = opt.arg; mode = STATS;    break;
		case 'q': file = opt.arg; mode = QUERY;    break;
		case 'V': file = opt.arg; mode = VALIDATE; break;
		case 'g': mode = GENERATE;                 break;
		case 'T': assert(opt.arg); tmp = opt.arg;  break;
		case 'b': ops.size   = opt.narg; break;
		case 'm': min        = opt.narg; break;
		case 'M': max        = opt.narg; break;
		case 'R': records    = opt.narg; break;
		case 'S': seed       = opt.narg; break;
		case 'o': ops.offset = opt.narg; break;
		default: help(stderr, argv[0]); return 1;
		}
	}

	/* N.B. We could also generate a CDB file directly as well,
	 * instead of generating a dump, the "generate" function
	 * would need a rewrite though */
	if (mode == GENERATE) {
		int r = generate(stdout, records, min, max, seed);
		/* Valgrind reports errors (on my setup) when writing to
		 * stdout and not flushing, the flush is called in the exit
		 * code and causes an error even though nothing *seems*
		 * incorrect. */
		if (fflush(stdout) < 0)
			r = -1;
		return r < 0 ? 1 : 0;
	}

	/* For many of the modes "file" could be "stdout", this works
	 * for everything bar CREATE mode which will need to seek on
	 * its output. */
	if (!file)
		return help(stderr, argv[0]), 1;

	creating = mode == CREATE;

	cdb_t *cdb = NULL;
	const char *name = creating && tmp ? tmp : file;
	info("opening '%s' for %s", name, creating ? "writing" : "reading");
	const int etmp = errno;
	errno = 0;
	if (cdb_open(&cdb, &ops, creating, name) < 0) {
		const char *f = errno ? strerror(errno) : "unknown";
		const char *m = creating ? "create" : "read";
		die("opening file '%s' in %s mode failed: %s", name, m, f);
	}
	errno = etmp;

	int r = 0;
	switch (mode) {
	case CREATE:   r = cdb_create(cdb, stdin);                                                       break;
	case DUMP:     r = cdb_foreach(cdb, cdb_dump,      stdout); if (fputc('\n', stdout) < 0) r = -1; break;
	case KEYS:     r = cdb_foreach(cdb, cdb_dump_keys, stdout); if (fputc('\n', stdout) < 0) r = -1; break;
	case STATS:    r = cdb_stats_print(cdb, stdout, 0, ops.size / 8ul);                              break;
	case VALIDATE: r = cdb_foreach(cdb, NULL, NULL);                                                 break;
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
	if (fflush(stdout) < 0)
		r = -1;

	const int cdbe = cdb_status(cdb);
	if (cdb_close(cdb) < 0)
		die("close failed: %d", cdbe);
	if (cdbe < 0)
		die("cdb internal error: %d", cdbe);

	if (creating && tmp) {
		info("renaming temporary file");
		if (rename(tmp, file) < 0)
			die("rename from '%s' to '%s' failed: %s", tmp, file, strerror(errno));
	}
	return r < 0 ? 1 : 0;
}

