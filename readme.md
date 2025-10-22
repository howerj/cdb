% cdb(1) | Constant Database

# NAME

CDB - An interface to the Constant Database Library

# SYNOPSES

cdb -h

cdb -\[cdkstVG\] file.cdb

cdb -q file.cdb key \[record#\]

cdb -g -M minimum -M maximum -R records -S seed

cdb -H

# DESCRIPTION

	Author:     Richard James Howe
	License:    Unlicense
	Repository: <https://github.com/howerj/cdb>
	Email:      howe.r.j.89@gmail.com

A clone of the [CDB][] database, a simple, read-only (once created) database.
The database library is designed so it can be embedded into a microcontroller
if needed and has very low memory requirements that are configurable. This 
program can be used for creating and querying CDB databases, which consist 
of key-value pairs of binary data.

This program also includes several options that help in testing out the
database, one for hashing input keys and printing the hash for the default hash
function and another one for generating a database with (Pseudo-)random keys
and values of a given length.

There are some example database available at
<https://github.com/howerj/cdbdbs> that can be used in conjunction with this
library or other CDB compatible tools for testing purposes.

**This library can create 16, 32 and 64 bit versions of the CDB file format**.

Originally CDB was only available in 32-bit format with many others extending
CDB into 64-bit versions which do not have the 4GiB limit on file size. A long
time after the first release the author, D.J Bernstein, made a 64-bit version,
available at <https://cdb.cr.yp.to/index.html>. This library is compatible
with both the 32 and 64 bit version of the file format.

This implementation can also create 16-bit versions of the file format, which
are much more limited than either with a maximum database size of 64KiB,
suitable only for constrained and very limited systems.


# OPTIONS

**-h** : print out this help message and exit successfully

**-b** : set the size of the CDB database to use (default is 32, can be 16 or 64)

**-v**: increase verbosity level

**-t** *file.cdb* : run internal tests, exit with zero on a pass

**-c**  *file.cdb* : run in create mode

**-d**  *file.cdb* : dump the database

**-k**  *file.cdb* : dump the keys in the database

**-s**  *file.cdb* : print statistics about the database

**-T** *temp.cdb* : name of temporary file to use

**-V**  *file.cdb* : validate database

**-q**  *file.cdb key record-number* : query the database for a key, with an optional record

**-o** number : specify offset into file where database begins

**-H** : hash keys and output their hash

**-g**  : spit out an example database to standard out

**-m** number   : set minimum length of generated record

**-M** number   : set maximum length of generated record

**-R** number   : set number of generated records

**-S** number   : set seed for record generation

# EXAMPLES

Creating a database, called 'example.cdb':

	$ ./cdb -c example.cdb
	+0,1:->X
	+1,0:Y->
	+1,1:a->b
	+1,1:a->b
	+1,2:a->ba
	+5,5:hello->world

Note that zero length keys and values are valid, and that duplicate keys are
allowed, even keys with the same value. A key with the specified value is
created for each duplicate, just like a non-duplicate key.

Looking up values in the created database:

	./cdb -q example.cdb ""
	./cdb -q example.cdb Y
	./cdb -q example.cdb a
	./cdb -q example.cdb a 0
	./cdb -q example.cdb a 1
	./cdb -q example.cdb a 2
	./cdb -q example.cdb hello

Dumping a database:

	$ ./cdb -d example.cdb

A database dump can be read straight back in to create another database:

	$ ./cdb -d example.cdb | ./cdb -c should_have_just_used_copy.cdb

Which is not useful in itself, but *assuming* your data (both keys and
values) is ASCII text with no new lines and NUL characters then you could
filter out, modify or add in values with the standard Unix command line
tools.

# RETURN VALUE

cdb returns zero on success/key found, and a non zero value on failure. Two is
returned if a key is not found when searching for keys, any other value 
indicates a more serious failure.

# LIMITATIONS

Three different versions of the library can be built; a 16, a 32 and a 64 bit
version. The 32 bit version is the default version. For all versions there is a
limit on the maximum file size in the format used of 2^N, where N is the size.
Keys and Values have the same limit (although they can never reach that size as
some of the overhead is taken up as part of the file format). Any other
arbitrary limitation is a bug in the implementation.

The minimum size of a CDB file is 256 \* 2 \* (N/8) bytes.

It should be noted that if you build an N-bit (where N is 16, 32 or 64) 
version of this library you are limited to creating databases that are the
size of N and less, e.g. If `cdb_word_t` is set to `uint32_t`, and therefore
the 32-bit version of this library is being built, then you can create 32-bit
and 16-bit versions of the CDB database format, but you cannot make 64-bit
versions. You can set `cdb_word_t` to `uint64_t` (which enables the library
to create all three mutually incompatible versions of the library) on a
32-bit system, naturally.

# INPUT/DUMP FORMAT

The input and dump format follow the same pattern, some ASCII text specifying
the beginning of a record and then some binary data with some separators, and
a newline terminating the record, the format is:

	+key-length,value-length:KEY->VALUE
	+key-length,value-length:KEY->VALUE
	...
	+key-length,value-length:KEY->VALUE

Despite the presence of textual data, the input key and value can contain
binary data, including the ASCII NUL characters or newlines.

An example, encoding the key value pair "abc" to "def" and "G" to "hello":

	+3,3:abc->def
	+1,5:G->hello

The following [awk][] script can be used to pre-process a series of key-value
pairs in the format "key value", with one record per line and optional comment
lines:

	#!/bin/sh
	LC_ALL='C' awk '
	  /^[^#]/ {
	    print "+" length($1) "," length($2) ":" $1 "->" $2
	  }
	  END {
	    print ""
	  }
	' | cdb -c "$@"

Which was available in the original [original cdb][] program as 'cdbmake-12'.

# FILE FORMAT

The file format is incredibly simple, it is designed so that only the header
and the hash table pointer need to be stored in memory during generation of the
table - the keys and values can be streamed on to the disk. The header consists
of 256 2-word values forming an initial hash table that point to the hash
tables at the end of the file, the key-value records, and then up to 256 hash
tables pointing to the key-value pairs.

A word consists of a 4-byte/32-bit value (or 2-bytes for the 16-bit format,
and 8-bytes for the 64-bit format). All word values are stored in little-endian 
format.

The initial hash table contains an array of 256 2-word values.
The words are; a position of a hash table in the file and the number of buckets
in that hash table, stored in that order. To lookup a key the key is first
hashed, the lowest eight bits of the hash are used to index into the initial table
and if there are values in this hash the search then proceeds to the second hash
table at the end of the file.

The hash tables at the end of the file, after the initial hash table and
key-value pairs, contains an array of two word records, with the full
hash and a file position of the key-value pair. To search for a key in this
table the hash of the key is taken and the lowest eight bits are discarded
by shifting right eight places, the hash is then taken modulo the number
of elements in the hash table, the resulting value is used as an initial
index into the hash table. Searching continues until the key is found, or
an empty record is found, or the number of records in the table have been
searched through with no match. A key is compared by looking at the hash
table records, if the hash of the key matches the stored hash in the hash
table records then a possible match is found, the file position is then used
to look up the key-value pair and the key is compared.

The number of buckets in the hash table is chosen as twice the number of
populated entries in the hash table, so the hash table does not become
too full degrading performance.

A key-value pair is stored as two words containing the key length and the value
length in that order, then the key, and finally the value.

The hashing algorithm used is similar to [djb2][] (except for the 64-bit
version of this library, which uses a 64-bit variant of SDBM hash, others use
djb2 extended to 64-bits), but with a minor modification that 
an exclusive-or replaces an addition. 

The algorithm calculates hashes of the size of a word, the initial hash
value is the special number '5381'.  The hash is calculated as the current
hash value multiplied by 33, to which the new byte to be hashes and the
result of multiplication under go an exclusive-or operation. This repeats
until all bytes to be hashed are processed. All arithmetic operations are
unsigned and performed modulo 2 raised to the power of 32.

The pseudo code for this is:

	set HASH to 5381
	for each OCTET in INPUT:
		set HASH to: ((HASH * 33) % pow(2, 32)) xor OCTET
	return HASH

Note that there is nothing in the file format that disallows duplicate keys in
the database, in fact the API allows duplicate keys to be retrieved. Both key
and data values can also be zero bytes long. There are also no special
alignment requirements on the data, data is packed as tightly as possible.

The best documentation on the file format is a small pure python script that
implements a set of functions for manipulating a CDB database, a description is
available here <http://www.unixuser.org/~euske/doc/cdbinternals/> and the
script itself is available at the bottom of that page at
<http://www.unixuser.org/~euske/doc/cdbinternals/pycdb.py>, it has also been
incorporated into the bottom of this file for posterity.

A visualization of the overall file structure:

	         Constant Database Sections
	.-------------------------------------------.
	|   256 Bucket Initial Hash Table (2KiB)    |
	.-------------------------------------------.
	|            Key Value Pairs                |
	.-------------------------------------------.
	|       0-256 Secondary Hash Tables         |
	.-------------------------------------------.

The initial hash table at the start of the file:

	    256 Bucket Initial Hash Table (2KiB)
	.-------------------------------------------.
	| { P, L } | { P, L } | { P, L } |   ...    |
	.----------+----------+----------+----------.
	|   ...    | { P, L } | { P, L } | { P, L } |
	.-------------------------------------------.
	P = Position of secondary hash table
	L = Number of buckets in secondary hash table

The key-value pairs:

	.-------------------------------------------.
	| { KL, VL } | KEY ...      | VALUE ...     |
	.-------------------------------------------.
	KL    = Key Length
	VL    = Value Length
	KEY   = Varible length binary data key
	VALUE = Variable length binary value

Of the variable number of hash tables (which each are of a variable length) at
the end of the file:

	 0-256 Variable Length Secondary Hash Tables
	.---------------------.
	| { H, P } | { H, P } |
	.----------+----------+---------------------.
	| { H, P } |   ...    |   ...    | { H, P } |
	.----------+----------+----------+----------.
	| { H, P } |   ...    | { H, P } |
	.--------------------------------.
	H = Hash
	P = Position of Key-Value Pair

And that is all for the file format description.

While the keys-value pairs can be streamed to disk and the second level hash
table written after those keys, anything that creates a database will have
to seek to the beginning of the file to rewrite the header, this could have
been avoided by storing the 256 initial hash table results at the end of
the file allowing a database to be constructed in a Unix filter, but alas,
this is not possible (and has some downsides). 

Also of note, by passing in a custom hash algorithm to the C API you have much
more control over where each of the key-value pairs get stored, specifically,
which bucket they will end up in by controlling the lowest 8-bits (for example
you could set the lowest 8-bits to the first byte in the key in a custom hash).

Note that there is nothing stopping you storing the key-value pairs in some
kind of order, you could do this by adding the keys in lexicographic order for
a database sorted by key. Retrieving keys using the C function "cdb\_foreach"
would allow you retrieve keys in order. The hash table itself would remain
unaware of this order. Dumping the key-value pairs would maintain this order
as well. There is no guarantee other tools will preserve this order however
(they may dump key-value pairs backwards, or by going through the hash table).

# CDB C API OVERVIEW

There are a few goals that the API has:

* Simplicity, there should be few functions and data structures.
* The API is easy to use.
* There should be minimal dependencies on the C standard library. The
  library itself should be small and not be a huge, non-portable, "optimized",
  mess.
* The user should decide when, where and how allocations are performed. The
  working set that is allocated should be small.
* The database driver should catch corrupt files if possible.

Some of these goals are in conflict, being able to control allocations and
having minimal dependencies allow the library to be used in an embedded system,
however it means that in order to do very basic things the user has to
provide a series of callbacks thus making the library more complicated to use. 

The callbacks are simple to implement on a hosted system, examples are
provided in [main.c][] and [host.c][] in the project repository, but this
means the library is not just read to use.

There are two sets of operations that most users will want to perform; creating
a database and reading keys. After the callbacks have been provided, to create
a database requires opening up a new database in create mode:

	/* error handling omitted for brevity */
	cdb_t *cdb = NULL;
	cdb_options_t ops = { /* Your file callbacks/options go here */ };
	cdb_open(&cdb, &ops, 1, "example.cdb");
	cdb_buffer_t key   = { .length = 5, .buffer = "hello", };
	cdb_buffer_t value = { .length = 5, .buffer = "world", };
	cdb_add(cdb, &key, &value);
	cdb_close(cdb);

If you are dealing with mostly NUL terminated ASCII/UTF-8 strings it is worth
creating a function to deal with them:

	int cdb_add_string(cdb_t *cdb, const char *key, const char *value) {
		assert(cdb);
		assert(key);
		assert(value);
		const cdb_buffer_t k = { .length = strlen(key),   .buffer = (char*)key,   };
		const cdb_buffer_t v = { .length = strlen(value), .buffer = (char*)value, };
		return cdb_add(cdb, &k, &v);
	}

Note that you *cannot* query for a key from a database opened up in create
mode and you *cannot* add a key-value pair to a database opened up in read
mode. The operations are mutually exclusive.

To search for a key within the database, you open up a database connection in
read mode (create = 0):

	/* error handling omitted for brevity */
	cdb_t *cdb = NULL;
	cdb_options_t ops = { /* Your file callbacks/options go here */ };
	cdb_open(&cdb, &ops, 1, "example.cdb");
	cdb_buffer_t key = { .length = 5, .buffer = "hello" };
	cdb_file_pos_t value = { 0, 0, };
	cdb_get(cdb, &key, &value);
	/* use cdb_seek, then cdb_read, to use returned value */
	cdb_close(cdb);

Upon retrieval of a key the database does not allocate a value for you, instead
it provides an object consisting of a file position and a length of the value.
This can be read from wherever the database is stored with the function
'cdb\_read'. Before issuing a read, 'cdb\_seek' *must* be called as the file
handle may be pointing to a different area in the database.

If a read or a seek is issued that goes outside of the bounds of the database
then all subsequent database operations on that handle will fail, not just
reads or seeks. The only valid things to do on a database that has returned a
negative number is to call 'cdb\_status' and then 'cdb\_close' and never
use the handle again. 'cdb\_status' must not be used on a closed handle.

As there are potentially duplicate keys, the function 'cdb\_count' can be
used to query for duplicates. It sets the parameter count to the number of
records found for that key (and it sets count to zero, and returns zero, if no
keys are found, it returns one if one or more keys were found).

The function 'cdb\_status' can be used to query what error has occurred, if
any. On an error a negative value is returned, the meaning of this value is
deliberately not included in the header as the errors recorded and the
meaning of their values may change. Use the source for the library to determine
what error occurred if needed. The reason for this is so that you can debug
problems if they happen but you are prevented from relying on the existence
of codes having specific meaning (which are subject to arbitrary change between
versions).

The function 'cdb\_version' returns the version number in an out parameter
and information about the compile time options selected when the library was built.
A [Semantic Version Number][] is used, which takes the form "MAJOR.MINOR.PATCH".
The PATCH number is stored in the Least Significant Byte, the MINOR number the
next byte up, and the MAJOR in the third byte. The fourth byte contains the
compile time options.

There are several things that could be done to speed up the database but this
would complicate the implementation and the API.

## C API FUNCTIONS

The C API contains 13 functions and some callbacks, more than is desired,
but they all have their uses. Ideally a library would contain far fewer
functions and require less of a cognitive burden on the user to get right,
however making a generic enough C library and using C in general requires
more complexity than is usual, but not more than is necessary.

There is regularity in these functions, they all return negative on failure
(the only exception being the allocator callback that returns a pointer),
most of the functions accept a "cdb\_t" structure as well, which is an
[opaque pointer][] (opaque pointers are not an unalloyed good, they imply
that an allocator must be used, which can be a problem in embedded systems).

	int cdb_open(cdb_t **cdb, const cdb_options_t *ops, int create, const char *file);
	int cdb_close(cdb_t *cdb);
	int cdb_read(cdb_t *cdb, void *buf, cdb_word_t length);
	int cdb_add(cdb_t *cdb, const cdb_buffer_t *key, const cdb_buffer_t *value);
	int cdb_seek(cdb_t *cdb, cdb_word_t position);
	int cdb_foreach(cdb_t *cdb, cdb_callback cb, void *param);
	int cdb_read_word_pair(cdb_t *cdb, cdb_word_t *w1, cdb_word_t *w2);
	int cdb_get(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value);
	int cdb_lookup(cdb_t *cdb, const cdb_buffer_t *key, cdb_file_pos_t *value, long record);
	int cdb_count(cdb_t *cdb, const cdb_buffer_t *key, long *count);
	int cdb_status(cdb_t *cdb);
	int cdb_version(unsigned long *version);
	int cdb_tests(const cdb_options_t *ops, const char *test_file);

	typedef int (*cdb_callback)(cdb_t *cdb, const cdb_file_pos_t *key, const cdb_file_pos_t *value, void *param);

* cdb\_open

The most complex function that contains the most parameters, "cdb\_open"
is used to open a connection to a database. A pointer to a handle is
passed to the first parameter, using the supplied allocation callback
(passed-in in the "ops" parameter) the function will allocate enough space
for "cdb\_t" structure, this out-parameter is the database handle. It will
be set to NULL on failure, which will also be indicated with a negative
return value on the "cdb\_open" function. Once "cdb\_close" is called on
this handle the handle *should not* be used again, and "cdb\_close" should
only be called on the returned handle *once*.

A single database can be opened by as many readers as you like, however
reading a database and writing to a database are mutually exclusive operations.

When writing to a database there *should not* be any readers active on
that database. This is a fundamental limitation of the database design.

Writing to a CDB file that is being read by another CDB instance can
cause corruption of data and general nasty things! Do not do it!

As such, a database can only be opened up in read only, or write only
mode.

The "file" parameter is passed to the "open" callback, which is present
in the "ops" parameter.

		void *(*open)(const char *name, int mode);

The callback should return an opaque pointer on success and NULL on failure.
It is used to open up a handle to the database via whatever method the
library user would like (for example, a simple file present in your file
system, or a section of flash in an embedded computer). The open callback
is used by "cdb\_open" and should not be called directly.

The "mode" parameter to the "open" callback will be set to "CDB\_RW\_MODE" if
"create" is non-zero, and will be set to "CDB\_RO\_MODE" if it is zero.

CDB\_RW\_MODE is an enumeration that has the value "1", whilst
CDB\_RO\_MODE has the value "0".

"cdb\_open" does quite a lot, when opening a CDB file for reading the
file is *partially* verified, when opening for writing a blank first level
hash table is written to disk. If either of this fails, then opening
the database will fail.

The function also needs the callbacks to perform a seek to be present,
along with the callback for reading. The write callback only needs to
present when the database is opened up in write mode.

* cdb\_close

This closes the CDB database handle, the handle may be NULL, if so, nothing
will be done. The same handle should not be passed in twice to "cdb\_close"
as this can cause double-free errors. This function will release any memory
and handles (by calling the "close" callback) associated with the handle.

When writing a database this function has one more task to do, and that
is finalizing the database, it writes out the hash-table at the end of the
file. If "cbd\_close" is not called after the last entry has been added then
the database will be in an invalid state and will not work (although the
database could technically be recovered either manually or with hypothetical
recovery tools).

This function may return negative on error, for example if the finalization
fails.

After calling "cdb\_close" the handle *must not* be used again.

* cdb\_read

To be used on a database opened up in read-mode only. This can
be used to read values, and keys, from the database. This
function does not call "cdb\_seek", the caller must call "cdb\_seek"
before calling this function to move the file pointer to the
desired location before reading. The file pointer will be updated
to point to after the location that has been read (or more accurately,
the read callback must do this). This function *does not* return the
number of bytes read, instead it returns zero for no error and
negative if an error condition occurs (a partial read is treated as
an error).

* cdb\_add

To be used on a database opened up in write, or creation, mode only.

This function adds a key-value pair to the database, which can be
looked up only after finalizing the database (by calling "cdb\_close")
and reopening the database in read-only mode, which should be done
after the final "cdb\_add" has been added.

It is unfortunate that both the key and value must reside within
memory, but doing anything else would complicate the API too much.

Once the key and value have been added they can be freed or discarded
however.

Adding key-value pairs consumes disk space (or wherever you chose to
store the data) and some extra memory which is needed to store the second 
level hash table, however the keys and values are not kept around in memory 
by the CDB library and can be freed by yourself after calling "cdb\_add".

Note that this function will add duplicate keys without complaining,
and can add zero length keys and values, likewise without complaining.

It is entirely up to the caller to prevent duplicates from being
added. This is one improvement that could be added to the library (as
you cannot check or query a partially written database at the
moment).


* cdb\_seek

This function changes the position that the next read or write
will occur from. You should not seek before or after the database,
doing so will result in an error. Seeking is always relative to the
start of the file, the optional offset specified in the CDB options
structure being added to the current position. Relative to current
position or file-end seeks cannot be done.

This function must be called before each call to "cdb\_read" or
"cdb\_read\_word\_pair", otherwise you may read garbage.

Calling "cdb\_seek" multiple times on the same location has no
effect (the "fseek" C standard library function may discard buffers
if called multiple times on the same location even though the file
position has not changed, resulting in a performance degradation).

There is currently no way to query the file position (although it
is stored internally to the library).

* cdb\_foreach

The "cdb\_foreach" function calls a callback for each value within the CDB
database. The callback is passed an optional "param", that you may use to
store a pointer to value or structure. If the callback returns negative or
a non-zero number then the for-each loop is terminated early (a positive
number is returned, a negative number results in -1 being returned). If the
callback returns zero then the next value, if any, is processed with the
callback being called again.

The callback is passed a structure which contains the location within the
CDB database that contains the key and value. The keys and values are not
presented in any specific order and the order should not be expected to stay
the same between calls.

To read either a key or a value you must call "cdb\_seek" before calling
"cdb\_read" yourself.

Passing in NULL is allowed and is not a No-Operation, it can be used to
check the integrity of the database as much as is possible without
checksums being present.

* cdb\_read\_word\_pair

To be used on a database opened up in read-mode only. This function
is a helper function that strictly does not need to exist, it is
used for reading two "cdb\_word\_t" values from the database. This
can be useful for the library user for more detailed analysis of
the database than would normally be possible, many values within
the database are stored as two "cdb\_word\_t" values. Looking inside at
the raw binary data of this read-only database is not discouraged as the 
file format is well documented.

This function does not call "cdb\_seek", that must be called
before hand to seek to the desired file location. The file position
will be updated to point after the two read values (by the read
callback provided by you!).

* cdb\_get

This function populates the "value" structure if the "key" is found
within the CDB database. The members of "value" will be set to zero
if a key is not found, if it is found the position will be non-zero,
although the length may be zero as zero length values, and keys, are
allowed.

Note that this function does not actually retrieve the key and put it into a
buffer, there is a very good reason for that. It would be easy enough to make
such a function given the functions present in this API, however in order
to make such a function it would have to do the following; allocate enough
space to store the value, read the value off of disk and then return the
result. This has massive performance implications. Imagine if a large value
is stored in the database, say a 1GiB value, this would mean at least 1GiB of
memory would need to be allocated, it would also mean all of the file buffers
would have been flushed and refilled, and all of that data would need to be
copied from disk to memory. This might be desired, it might also be *very*
wasteful, especially if only a fraction of the value is actually needed
(say the first few hundred bytes). There are also systems that have very
little RAM and a large amount of storage, if the API allocated internally
it would be impossible to read these values off of disk (a common error in
many C libraries that prevent their usage in embedded systems). Whether this
is wasteful depends entirely on your workload and use-cases for the database.

It is better to give the user tools to do what they need rather than 
insisting it be done one, limiting, although "easy", way.

This does mean that to actually retrieve the value the user must perform
their own "cdb\_seek" and "cdb\_read" operations. This means that the entire
value does not need to read into memory be the consumer, and potentially be
processed block by block by the "read" callback if needed.

* cdb\_lookup

"cdb\_lookup" is similar to "cdb\_get" except it accepts an optional
record number. Everything that applies to the get-function applies to the
lookup-function, the only difference is the record number argument (internally
"cdb\_get" is implemented with "cdb\_lookup").

If there are two or more keys that are identical then the question of how
to select a specific key arises. This is done with an arbitrary number that
will most likely, but is not guaranteed, to be the order in which the key
was added into the database, with the first value being zero and the index
being incremented from there on out.

If the key is found but the index is out of bounds it is treated as if the
key does not exist. Use "cdb\_count" to calculate the maximum number records
per key if needed, it is far more expensive to repeatedly call "cdb\_lookup"
on a key until it returns "key not found" to determine the number of duplicate
keys than it is to call "cdb\_count".

The index argument perhaps should be a "cdb\_word\_t", but there is always
debate around these topics (personally if I were to design a modern C-like
programming language everything integer related would default to 64-bits,
and all pointers would fit within that, other types for indexing and the
like would also be 64-bit. This is not a criticism of C, the madness around
integer types was born out of necessity where the word size could not even
be guaranteed to be a multiple of eight).

* cdb\_count

The "cdb\_count" function counts the number of entries that have the same
key value. This function requires multiple seeks and reads to compute, so
the returned value should be cached if you plan on using it again as the
value is expensive to calculate.

If the key is not found, a value indicating that will be returned and the count
argument will be zeroed. If found, the count will be put in the count argument.

* cdb\_status

This function returns the status of the CDB library handle. All
errors are sticky in this library, if an error occurs when handling
a CDB database then there is no way to clear that error short of
reopening the database with a new handle. The only valid operation
to do after getting an error from any of the functions that operate
on a "cdb\_t" handle is to call "cdb\_status" to query the error
value that is stored internally, and to call "cdb\_close" on that
handle (only once).

You cannot call "cdb\_status" on a closed handle.

"cdb\_status" should return a zero on no error and a negative value
on failure. It should not return a positive non-zero value.

* cdb\_version

"cdb\_version" returns the version number of the library. It stores
the value in an unsigned long. This may return an error value and a
zero value if the version has not been set correctly at compile time.

The value is stored in "MAJOR.MINOR.PATH" format, with "PATH" stored
in the Least Significant Byte. This is a semantic version number. If
the "MAJOR" number has changed then there are potentially breaking
changes in the API or ABI of this library that have been introduced,
no matter how trivial.

* cdb\_tests

And the callback for "cdb\_foreach":

* "cdb\_callback"

This callback is called for each value within the CDB database when used with
"cdb\_foreach". If a negative value is returned from this callback then the
foreach loop will end early and an error value will be returned. If the
value returned is greater than zero then the foreach loop will terminate
potentially early. If zero the foreach loop will continue to the next
key-value pair if available.

Each time this callback is called by "cdb\_foreach" it will be passed in a
key-value pair in the form of two length/file-location structures. You will
need to seek to those locations and call read the key-values yourself. There
is no guarantee the file position is in the correct location (ie. Pointing
to the location of the key), so call "cdb\_seek" before calling "cdb\_read".

There is no guarantee that the key-value pairs will be presented in the same
order each time the function is called and should not be counted on. There
is no attempt to preserve order.

See "cdb\_foreach" for more information.

## C API STRUCTURES

The C API has two simple structures and one complex one, the latter being
more of a container for callbacks (or, some might say, a way of doing object
oriented programming in C). The complex structure, "cdb\_options\_t", is an
unfortunate necessity.

The other two structures, "cdb\_buffer\_t" and "cdb\_file\_pos\_t", are
simple enough and need very little explanation, although they will be.

Let us look at the "cdb\_options\_t" structure:

	typedef struct {
		void *(*allocator)(void *arena, void *ptr, size_t oldsz, size_t newsz);
		cdb_word_t (*hash)(const uint8_t *data, size_t length);
		int (*compare)(const void *a, const void *b, size_t length);
		cdb_word_t (*read)(void *file, void *buf, size_t length);
		cdb_word_t (*write)(void *file, void *buf, size_t length);
		int (*seek)(void *file, uint64_t offset);
		void *(*open)(const char *name, int mode);
		int (*close)(void *file);
		int (*flush)(void *file);

		void *arena;
		cdb_word_t offset;
		unsigned size;
	} cdb_options_t;

Each member of the structure will need an explanation.

## STRUCTURE CALLBACKS

* allocator

This function is based off of the allocator callback mechanism
present in Lua, see <https://www.lua.org/manual/5.1/manual.html#lua_setallocf>
for more information on that allocator. This function can handle
freeing memory, allocating memory, and reallocating memory, all
in one function. This allows the user of this library to specify
where objects are allocated and how.

The arguments to the callback mean:

1. arena

This may be NULL, it is an optional argument that can be used
to store memory allocation statistics or as part of an arena
allocator.

2. ptr

This should be NULL if allocating new memory, of be a pointer
to some previously allocated memory if freeing memory or
reallocating it.

3. oldsz

The old size of the pointer if known, if unknown, use zero. This is
used to prevent unnecessary allocations.

4. newz

The new size of the desired pointer, this should be non-zero
if reallocating or allocating memory. To free memory set this
to zero, along with providing a pointer to free. If this is zero
and the "ptr" is NULL then nothing will happen.

5. The return value

This will be NULL on failure if allocating memory or reallocating
memory and that operation failed. It will be non-NULL on success,
containing usable memory. If freeing memory this should return NULL.

An example allocator using the built in allocation routines is:

	void *allocator_cb(void *arena, void *ptr, size_t oldsz, size_t newsz) {
		UNUSED(arena);
		if (newsz == 0) {
			free(ptr);
			return NULL;
		}
		if (newsz > oldsz)
			return realloc(ptr, newsz);
		return ptr;
	}

This callback is both simple and flexible, and more importantly
puts the control of allocating back to the user (I know I have
repeated this *many* times throughout this document, but it is
worth repeating!).

	compare: /* key comparison function: NULL defaults to memcmp */
	write: /* (conditional optional) needed for db creation only */
	flush: /* (optional) called at end of successful creation */

	arena:   /* used for 'arena' argument for the allocator, can be NULL if allocator allows it */
	offset: /* starting offset for CDB file if not at beginning of file */
	size:  /* Either 0 (same as 32), 16, 32 or 64, but cannot be bigger than 'sizeof(cdb_word_t)*8' */

* hash (optional)

The "hash" callback can be set to NULL, if that is the case then
the default hash, based off of djb2 and present in the original
CDB library, will be used. If you do provide your own hash function
you will effectively make this database incompatible with the standard
CDB format but there are valid reasons for you do do this, you might
need a stronger hash that is more resistant to denial of service attacks,
or perhaps you want similar keys to *collide* more to group them together.

The hash function returns "cdb\_word\_t" so the number of bits this
function returns is dependent on big that type is (determined at
compile time).

* compare (optional)

This function compares keys for a match, the function should behave like
[memcmp][], returning the same values on a match and a failure. You
may want to change this function if you want to compare keys partially,
however you will also need to change the hash function to ensure keys are
sorted into the right 256 buckets for your comparison (for example, with
the default hash function two keys with the same prefix could be stored in
two separate buckets).

### FILE CALLBACKS

The following callbacks act in a similar way to the file functions present
in [stdio.h][]. The only function missing is an [ftell][] equivalent.

* read

This function is used to read data out of the database, wherever that
data is stored. Unlike [fread][] a status code is returned instead of
the length of the data read, negative indicating failure. A partial read
should result in a failure. The only thing lacking from this callback
is a way to signal to perform non-blocking Input and Output, that would
complicate the internals however. The "read" callback should always be
present.

The first parameter, "file", is a handle to an object returned by the
"open" callback.

The callback should return 0 indicating no error if "length" bytes have
been read into "buf".

Reading should continue from the previous file pointer position, that
is if you open a file handle, read X bytes, the next time you read Y
bytes they should be read from the end of the X bytes and not the
beginning of the file (hence why read does not take a file position).

If implementing read callbacks in an embedded system you might have to
also implement that behavior.

* write (conditionally optional, needed for database creation only)

Similar to the "read" callback, but instead writes data into wherever
the database is stored.

* seek

This callback sets the file position that subsequent reads and writes
occur from.

* open

This callback should open the resource specified by the "name" string
(which will usually be a file name). There are two modes a read/write
mode (used to create the database) and a read-only mode. This callback
much like the "close" callback will only be called once internally
by the CDB library.

* close

This callback should close the file handle returned by "open", freeing
any resources associated with that handle.

* flush (optional)

An optional callback used for flushing writes to mass-storage. If NULL
then the function will not be called.

## STRUCTURE VARIABLES

* arena (optional, can be NULL, depends on your allocator)

This value is passed into the allocator as the "arena" argument whenever
the allocator is called. It can be NULL, which will usually be the case
if you are just using "malloc", "realloc" and "free" to implement the
allocator, but if you are implementing your own arena based allocator you
might want to set it to point to your arena (hence the name).

* offset

This offset can be used for CDB databases embedded within a file. If
the CDB database does not begin at the start of the file (or flash, or
wherever) then you can set this offset to skip over that many number
of bytes in the file.

* size

The size variable, which can be left at zero, is used to select
the word size of the database, this has an interaction with "cdb\_word\_t".

Missing perhaps is a unsigned field that could contain options
in each bit position in that field.


## BUFFER STRUCTURE

	typedef struct {
		cdb_word_t length; /* length of data */
		char *buffer;      /* pointer to arbitrary data */
	} cdb_buffer_t; /* used to represent a key or value in memory */

## FILE POSITION STRUCTURE

	typedef struct {
		cdb_word_t position; /* position in file, for use with cdb_read/cdb_seek */
		cdb_word_t length;   /* length of data on disk, for use with cdb_read */
	} cdb_file_pos_t; /* used to represent a value on disk that can be accessed via 'cdb_options_t' */

## EMBEDDED SUITABILITY

There are many libraries written in C, for better or worse, as it is the
lingua franca for software development at the moment. Few of those libraries
are directly suitable for use in [Embedded systems][] and are much less
flexible than they could be in general. Embedded systems pose some interesting
constraints (eschewing allocation via "malloc", lack of a file-system, and
more). By designing the library for an embedded system we can make a library
more useful not only for those systems but for hosted systems as well (e.g. By
providing callbacks for the FILE functions we can redirect them to wherever
we like, the CDB file could be stored remotely and accessed via TCP, or it
could be stored locally using a normal file, or it could be stored in memory).

There are two sets of functions that should be abstracted out in nearly
every C library, memory allocation (or even better, the caller can pass in
fixed length structures if possible) and Input/Output functions (including
logging!). This library does both.

There is one area in which the library is lacking, the I/O functions do not
yield if there is nothing to read yet, or a write operation is taking too
long. This does impose constraints on the caller and how the library is used
(all calls to the library could block for an arbitrary length of time). The
callbacks could return a status indicating the caller should yield, but
yielding and restoring state to enable partially completed I/O to finish
would greatly complicate the library (this would be trivial to implement if
C had portable coroutines built into the language).

More libraries should be written with this information in mind.

## TEST SUITE

There is a special note that should be mentioned about how the test suite
is handled as it is important.

It is difficult to make a good API that is easy to use, consistent, and
difficult to *misuse*. Bad APIs abound in common and critical software
(names will not be named) and can make an already difficult to use language
like C even more difficult to use.

One mistake that is often seen is API functionality that is conditional
upon an macro. This complicates the build system along with every piece of
software that is dependent on those optional calls. The most common function
to be optionally compiled in are test suite related functions if they are
present, another is logging. For good reason these test suites might need 
to be removed from builds (as they might take up large amounts of space for 
their code even if they are not needed, which is at a premium in embedded 
systems with limited flash memory).

The header often contains code like this:

	#ifdef LIBRARY_UNIT_TESTS
	int library_unit_tests(void);
	#endif

And the code like this, in C like pseudo-code:

	#ifdef LIBRARY_UNIT_TESTS
	int test_function_1(void) {
		/* might call malloc directly, making this unsuitable
		to be included in an embedded system */
		return result;
	}

	int library_unit_tests(void) {
		/* tests go here */
		if (test_function_1() != OK)
			return FAIL;
		return PASS;
	}
	#endif


In order to call this code you need to be aware of the "LIBRARY\_UNIT\_TESTS"
macro each time the function "library\_unit\_tests" is called, and worse,
whether or not your library was compiled with that macro enabled resulting
in link-time errors. Another common mistake is not passing in the functions
for I/O and allocation to the unit test framework, making it unsuitable for
embedded use (but that is a common criticism for many C libraries and not
just unit tests).

Compare this to this libraries way of handling unit tests:

In the header:

	int cdb_tests(const cdb_options_t *ops, const char *test_file);

And the *relevant* bits of code/pseudo-code:

	static uint64_t xorshift128(uint64_t s[2]) {
		assert(s);
		/* XORSHIFT-128 algorithm */
		return NEXT_PRNG;
	}

	int cdb_tests(const cdb_options_t *ops, const char *test_file) {
		assert(ops);
		assert(test_file);
		BUILD_BUG_ON(sizeof (cdb_word_t) < 2);

		if (CDB_TESTS_ON == 0)
			return CDB_OK_E;

		/* LOTS OF TEST CODE NOT SHOWN, some of which
		uses "xorshift128". */

		return STATUS;
	}

There is no "ifdef" surrounding any of the code (using "ifdef" anywhere to
conditionally execute code is usually a mistake, is only used within the
project to set default macro values if the macro is not previously
defined, an acceptable usage).

Two things are important here, the first, all of the Input and Output
and memory related functions are passed in via the "ops" structure,
as mentioned. This means that the test code is easy to port and run on
a microcontroller which might not have a file system (for testing and
development purposes you might want to run the tests on a microcontroller
but not keep them in in the final product).

The main difference is the lack of "ifdef" guards, instead if the macro
"CDB\_TESTS\_ON" is false the function "cdb\_tests" returns "CDB\_OK\_E"
(there is some debate if the return code should be this, or something
to indicate the tests are not present, but that is a separate issue, the
important bit is the return depending on whether the tests are present).

This "if" statement is a *far superior* way of handling optional code in
general. The caller does not have to worry if the function is present or
not, as the function will always be present in the library. Not only that,
but if the tests are not run because the compile time macro "CDB\_TESTS\_ON"
is false then the compiler will optimize out those tests even on the lowest
optimization settings (on any decent compiler).

This also has the advantage that the code that is not run still goes
through the compilation step meaning the code is less likely to be wrong
when refactoring code. Not only that, but because "xorshift128" which
"cdb\_tests" depends on, is declared to be static, if "CDB\_TESTS\_ON" is
false it to will be eliminated from the compiled object file so long as no
other function calls it. In actual fact, the code has changed since
this has been written and "cdb\_prng" is exposed in the header as it is
useful in [main.c][], "cdb\_prng" is backed by "xorshift128", so is no
longer static.

It should be noted that this does not just apply to unit tests, as mentioned
this can also apply to logging code, or any code that is to be optionally
compiled in.

	#ifndef CONFIG_OPTION_BLAH
	#define CONFIG_OPTION_BLAH (0)
	#endif

	void func(void) {
		if (CONFIG_OPTION_BLAH) {
			/* conditional code */
		}
	}

Is much easier to maintain and read than:

	void func(void) {
	#ifdef CONFIG_OPTION_BLAH
		/* conditional code */
	#endif
	}


# BUILD REQUIREMENTS

If you are building the program from the repository at
<https://github.com/howerj/cdb> you will need [GNU Make][] and a [C
Compiler][].  The library is written in pure [C99][] and should be fairly
simple to port to another platform. Other [Make][] implementations may
work, however they have not been tested. [git][] is also used as part of
the build system.

First clone the repository and change directory to the newly clone repository:

	git clone https://github.com/howerj/cdb cdb
	cd cdb

Type 'make' to build the *cdb* executable and library.

Type 'make test' to build and run the *cdb* internal tests. The script called
't', written in [sh][], does more testing, and tests that the user interface
is working correctly. 'make dist' is used to create a compressed tar file for
distribution. 'make install' can be used to install the binaries, however the
default installation directory (which can be set with the 'DESTDIR' makefile
variable) installs to a directory called 'install' within the repository -
it will not actually install anything. Changing 'DESTDIR' to '/usr' should
install everything properly. [pandoc][] is required to build the manual page
for installation, which is generated from this [markdown][] file.

Look at the source file [cdb.c][] to see what compile time options can be
passed to the compiler to enable and disable features (if code size is a
concern then the ability to create databases can be removed, for example).

# RENAME

CDB databases are meant to be read-only, in order to add entries to
a database that database should be dumped and new values added in along
with the old ones. That is, to add in a new value to the database the
entire database has to be rebuilt. This is not a problem for *some* work
loads, for *some* work loads the database could be rebuilt every X hours.

If this does present a problem, then you should not use this database.

However, when a database does have to be rebuilt how do you make sure
that users of it point to the new database and not the old one?

If you access the database via the command line applications then
the "[rename][]" function, which is atomic on POSIX systems, will do
what is needed. This is, a mechanism to swap out the old database with
a new one without affecting any of the current readers.

A rename can be done in C like so:

	rename("new.cdb", "current.cdb"); /* Atomic rename */

If a reader opens "current.cdb" before the rename then it will continue
to read the old database until it closes the handle and opens up "current.cdb"
after the rename. The files data persists even if there is no file name that
points to it so long as there are active users of that file (ie. If a file
handle to that file is still open). This will mean that there could be
processes that use old data, but not inconsistent data. If a reader opens
up the data after the rename, it will get the new data.

This also means that the writer should never write to a file that is
currently in use by other readers or writers, it should write to a new
file that will be renamed to the file in use, and it also means that a
large amount of disk storage space will be in use until all users of
the old databases switch to the new databases allowing the disk space
to be reclaimed by the operating system.

# POSSIBLE DIRECTIONS

There are many additions that could be made to a project, however the
code is quite compact and neat, anything else that is needed could be built
on top of this library. Some ideas for improvement include; adding a header
along with a [CRC][], adding (unsafe) functions for rewriting key-values,
adding (de)compression (with the [shrink][] library) and decryption,
integrating the project in an embedded system in conjunction with [littlefs][]
as an example, allowing the user to supply their own comparison and hash
functions, adding types and schemas to the database, and more. The project
could also be used as the primary database library for the [pickle][]
interpreter, or for serving static content in the [eweb][] web-server.

All of these would add complexity, and more code - making it more useful
to some and less to others. As such, apart from bugs, the library and test
driver programs should be considered complete.

The lack of a header might be solved in creative ways as:

* The integrity of most of the file can be checked by making sure all pointers are
  within bounds, that key-value pairs are stored one after another and that
  each key is in the right bucket for that hash. The only things not checked
  would be the values (they would still have to be of the right length).
* If a file successfully passes a verification it can be identified as a valid
  CDB file of that size, this means we would not need to store header
  information about the file type and structure. This has been verified
  experimentally (the empty and randomly generated databases of a different
  size do not pass verification when the incorrect size is specified with
  the "-b" option).
* We could place the header within the key-value section of the database, or
  even at the end of the file.

Things that *should* and *could* be done, but have not:

* Fuzzing with [American Fuzzy Lop][] to iron out the most egregious
bugs, security relevant or otherwise. This has been used on the [pickle][]
library to great effect and it finds bugs that would not be caught be unit
testing alone. **The library is currently undergoing fuzzing, nothing
bad found so far**.
* The current library implements a system for looking up data
stored to disk, a *system* could be created that does so much more.
Amongst the things that could be done are:
  - Using the CDB file format only as a serialization format
  for an in memory database which would allow key deletion/replacing.
  This Key-Value store would essentially just be an in memory hash
  table with a fancy name, backed by this library. The project could
  be done as part of this library or as a separate project.
  - Implementing the [memcached protocol][] to allow remote querying
  of data.
  - Alternatively make a custom protocol that accept commands over
  UDP.
There are a few implementation strategies for doing this.
* Alternatively, just a simple Key-Value store that uses this database
as a back-end without anything else fancy.
* Changing the library interface so it is a [header only][] C library.
* Making a set of callbacks to allow an in memory CDB database, useful
for embedding the database within binaries.
* Designing a suite of benchmarks for similar databases and implementations
of CDB, much like <https://docs.huihoo.com/qdbm/benchmark.pdf>.

Porting this to Rust and making a crate for it would be nice,
[although implementations already exists](https://crates.io/search?q=cdb).
Just making bindings for this library would be a good initial step, along
with other languages.

For more things that are possible to do:

* The API supplies a for-each loop mechanism where the user supplies a
callback, an iterator based solution would be more flexible (but slightly
more error prone to use).
* The user can specify their own hash algorithm, using one with perhaps
better characteristics for their purposes (and breaking compatibility
with the original format). One interesting possibility is using a hashing
algorithm that maximizes collisions of similar keys, so similar keys are
grouped together which may be useful when iterating over the database. 
Unfortunately the initial 256 wide bucket system interferes with this, 
which could be remedied by returning zero for lowest eight bits, degrading 
performance. It is not really viable to do this with this system, but
hashing algorithms that maximize collisions, such as [SOUNDEX][], are
interesting and deserve a mention. This could be paired with a user
supplied comparison function for comparing the keys themselves.
* The callbacks for the file access words ("open", "read", ...) deserve
their own structure so it can be reused, as the allocator can, although
it may require some changes to how those functions work (such as different
return values, passing in a handle to arbitrary user supplied data, and
more).
* Options for making the file checking more lax, as information could
be stored between the different key/value pairs making the file format
semi-compatible between implementations. This could be information usually
stored in the header, or information about the key/values themselves (such
as type information). Some implementations, including this one, are
more strict in what they accept.
* Some of the functions in [main.c][] could be moved into [cdb.c][] so
users do not have to reimplement them.
* A poor performance [Bloom Filter][] like algorithm can be made 
using the first level hash table. A function to return whether an
item may be in the set or is definitely not can be made by checking
whether there are any items in the first 256 bucket that key hashes
to. The 256 bucket is small enough to fit in memory, as are the second
level hash tables which could be used to improve performance even more.
* If the user presorts the keys when adding the data then the keys can
be retrieved in order using the "foreach" API call. The user could sort
on the data instead if they like.
* The way version information is communicated within the API is not
perhaps the best way of doing it. A simple macro would suffice.
* The file format really could use a redesign. One improvement apart
from adding a header would be to move the 256 bucket initial hash table
to the end of the file so the entire file format could be streamed to
disk.

# A BETTER FORMAT

A better format would do the following:

* Have a format identifier at the beginning of the file.
* Use a 64-bit hash and 64-bit pointers, the extra space this requires versus a
32-bit pointer is negligible. The 64-bit version of this library allows for the
creation of a 64-bit non-compatible version of the CDB file. The newer version
of CDB library does this, as does this one.
* Instead of going back to the beginning of the file the top level of the two level 
hash table could be dumped at the end of the file. This has some disadvantages, but 
not many. It does mean that the length of the file cannot be determined from within
the file (which it can with CDB, although some calculation and searching is
required), and it also means that the hash table is not on a page boundary.
* Include a CRC of the file, this could be done out of bounds for a normal
CDB file, but there is no reason not to include a CRC. It can be optionally
skipped upon opening the file if speed is of concern.

These issues are minor, and have some drawbacks, they are also not sufficient
to create a new format that is incompatible with all the tooling already out
there.

# BUGS

For any bugs, email the [author][]. It comes with a 'works on my machine
guarantee'. The code has been written with the intention of being portable,
and should work on 32-bit and 64-bit machines. It is tested more frequently
on a 64-bit Linux machine, and less frequently on Windows. Please give a
detailed bug report (including but not limited to what machine/OS you are
running on, compiler, compiler version, a failing example test case, your
blood type and star sign, etcetera).

# PYTHON IMPLEMENTATION

Available from here
<https://www.unixuser.org/~euske/doc/cdbinternals/index.html>. It
probably is the most succinct description and understandable by someone
not versed in python.

	#!/usr/bin/env python

	# Python implementation of cdb

	# calc hash value with a given key
	def calc_hash(s):
	  return reduce(lambda h,c: (((h << 5) + h) ^ ord(c)) & 0xffffffffL, s, 5381)

	# cdbget(fp, basepos, key)
	def cdbget(fp, pos_header, k):
	  from struct import unpack

	  r = []
	  h = calc_hash(k)

	  fp.seek(pos_header + (h % 256)*(4+4))
	  (pos_bucket, ncells) = unpack('<LL', fp.read(4+4))
	  if ncells == 0: raise KeyError

	  start = (h >> 8) % ncells
	  for i in range(ncells):
	    fp.seek(pos_bucket + ((start+i) % ncells)*(4+4))
	    (h1, p1) = unpack('<LL', fp.read(4+4))
	    if p1 == 0: raise KeyError
	    if h1 == h:
	      fp.seek(p1)
	      (klen, vlen) = unpack('<LL', fp.read(4+4))
	      k1 = fp.read(klen)
	      v1 = fp.read(vlen)
	      if k1 == k:
		r.append(v1)
		break
	  else:
	    raise KeyError

	  return r


	# cdbmake(filename, hash)
	def cdbmake(f, a):
	  from struct import pack

	  # write cdb
	  def write_cdb(fp):
	    pos_header = fp.tell()

	    # skip header
	    p = pos_header+(4+4)*256  # sizeof((h,p))*256
	    fp.seek(p)

	    bucket = [ [] for i in range(256) ]
	    # write data & make hash
	    for (k,v) in a.iteritems():
	      fp.write(pack('<LL',len(k), len(v)))
	      fp.write(k)
	      fp.write(v)
	      h = calc_hash(k)
	      bucket[h % 256].append((h,p))
	      # sizeof(keylen)+sizeof(datalen)+sizeof(key)+sizeof(data)
	      p += 4+4+len(k)+len(v)

	    pos_hash = p
	    # write hashes
	    for b1 in bucket:
	      if b1:
		ncells = len(b1)*2
		cell = [ (0,0) for i in range(ncells) ]
		for (h,p) in b1:
		  i = (h >> 8) % ncells
		  while cell[i][1]:  # is call[i] already occupied?
		    i = (i+1) % ncells
		  cell[i] = (h,p)
		for (h,p) in cell:
		  fp.write(pack('<LL', h, p))

	    # write header
	    fp.seek(pos_header)
	    for b1 in bucket:
	      fp.write(pack('<LL', pos_hash, len(b1)*2))
	      pos_hash += (len(b1)*2)*(4+4)
	    return

	  # main
	  fp=file(f, "wb")
	  write_cdb(fp)
	  fp.close()
	  return


	# cdbmake by python-cdb
	def cdbmake_true(f, a):
	  import cdb
	  c = cdb.cdbmake(f, f+".tmp")
	  for (k,v) in a.iteritems():
	    c.add(k,v)
	  c.finish()
	  return


	# test suite
	def test(n):
	  import os
	  from random import randint
	  a = {}
	  def randstr():
	    return "".join([ chr(randint(32,126)) for i in xrange(randint(1,1000)) ])
	  for i in xrange(n):
	    a[randstr()] = randstr()
	  #a = {"a":"1", "bcd":"234", "def":"567"}
	  #a = {"a":"1"}
	  cdbmake("my.cdb", a)
	  cdbmake_true("true.cdb", a)
	  # check the correctness
	  os.system("cmp my.cdb true.cdb")

	  fp = file("my.cdb")
	  # check if all values are correctly obtained
	  for (k,v) in a.iteritems():
	    (v1,) = cdbget(fp, 0, k)
	    assert v1 == v, "diff: "+repr(k)
	  # check if nonexistent keys get error
	  for i in xrange(n*2):
	    k = randstr()
	    try:
	      v = a[k]
	    except KeyError:
	      try:
		cdbget(fp, 0, k)
		assert 0, "found: "+k
	      except KeyError:
		pass
	  fp.close()
	  return

	if __name__ == "__main__":
	  test(1000)

This tests the python version implemented here against another python
implementation. It only implements the original 32-bit version.

# COPYRIGHT

The libraries, documentation, and the test driver program are licensed under
the [Unlicense][]. Do what thou wilt.

[author]: howe.r.j.89@gmail.com
[main.c]: main.c
[cdb.c]: cdb.c
[host.c]: host.c
[CDB]: https://cr.yp.to/cdb.html
[GNU Make]: https://www.gnu.org/software/make/
[C Compiler]: https://gcc.gnu.org/
[C99]: https://en.wikipedia.org/wiki/C99
[littlefs]: https://github.com/ARMmbed/littlefs
[CRC]: https://en.wikipedia.org/wiki/Cyclic_redundancy_check
[shrink]: https://github.com/howerj/shrink
[djb2]: http://www.cse.yorku.ca/~oz/hash.html
[ronn]: https://www.mankier.com/1/ronn
[pandoc]: https://pandoc.org/
[Unlicense]: https://en.wikipedia.org/wiki/Unlicense
[Make]: https://en.wikipedia.org/wiki/Make_(software)
[sh]: https://en.wikipedia.org/wiki/Bourne_shell
[git]: https://git-scm.com/
[markdown]: https://daringfireball.net/projects/markdown/
[American Fuzzy Lop]: http://lcamtuf.coredump.cx/afl/
[Semantic Version Number]: https://semver.org/
[awk]: https://en.wikipedia.org/wiki/AWK
[original cdb]: https://cr.yp.to/cdb.html
[pickle]: https://github.com/howerj/pickle
[eweb]: https://github.com/howerj/eweb
[binary file format]: https://stackoverflow.com/questions/323604
[memcached protocol]: https://raw.githubusercontent.com/memcached/memcached/master/doc/protocol.txt
[header only]: https://en.wikipedia.org/wiki/Header-only
[Embedded systems]: https://en.wikipedia.org/wiki/Embedded_system
[opaque pointer]: https://en.wikipedia.org/wiki/Opaque_pointer
[rename]: https://cplusplus.com/reference/cstdio/rename/
[memcmp]: https://cplusplus.com/reference/cstring/memcmp/
[stdio.h]: https://cplusplus.com/reference/cstdio/
[fread]: https://cplusplus.com/reference/cstdio/fread/
[ftell]: https://cplusplus.com/reference/cstdio/ftell/
[SOUNDEX]: https://en.wikipedia.org/wiki/Soundex
[Bloom Filter]: https://en.wikipedia.org/wiki/Bloom_filter
