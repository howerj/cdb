% cdb(1) | Constant Database

# NAME

CDB - An interface to the Constant Database Library

# SYNOPSES

cdb -h

cdb -\[cdkstV\] file.cdb

cdb -q file.cdb key \[record#\]

# DESCRIPTION

	Author:     Richard James Howe
	License:    Unlicense
	Repository: <https://github.com/howerj/cdb>
	Email:      howe.r.j.89@gmail.com

A clone of the [CDB][] database, a simple, read-only (once created) database.
The database library is designed so it can be embedded into a microcontroller
if needed. This program can be used for creating and querying CDB databases,
which consist of key-value pairs of binary data.

# OPTIONS

**-h** : print out this help message and exit successfully

**-v**: increase verbosity level

**-t** *file.cdb* : run internal tests, exit with zero on a pass

**-c**  *file.cdb* : run in create mode

**-d**  *file.cdb* : dump the database

**-k**  *file.cdb* : dump the keys in the database

**-s**  *file.cdb* : print statistics about the database

**-T** *temp.cdb* : name of temporary file to use

**-V**  *file.cdb* : validate database

**-q**  *file.cdb key record-number* : query the database for a key, with an optional record


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
allowed, even keys with the same value, a key with the specified value is
created for each duplicate, just like a non-duplicate key.

Looking up values in the created database:

	./cdb -q example.cdb ""
	./cdb -q example.cdb Y
	./cdb -q example.cdb a
	./cdb -q example.cdb a 0
	./cdb -q example.cdb a 1
	./cdb -q example.cdb a 2
	./cdb -q example.cdb hello

This looks up the keys.

Dumping a database:

	$ ./cdb -d example.cdb

A database dump can be read straight back in to create another database:

	$ ./cdb -d example.cdb | ./cdb -c should_have_just_used_copy.cdb


# RETURN VALUE

cdb returns zero on success/key found, and a non zero value on failure. Two is
returned if a key is not found, any other value indicates a more serious
failure.

# LIMITATIONS

Three different versions of the library can be built; a 16, a 32 and a 64 bit
version. The 32 bit version is the default version. For all versions there is a
limit on the maximum file size in the format used of 2^N, where N is the size.
Keys and Values have the same limit (although they can never reach that size as
some of the overhead is taken up as part of the file format). Any other
arbitrary limitation is a bug in the implementation.

The minimum size of a CDB file is 256 \* 2 \* (N/8) bytes.

# INPUT/DUMP FORMAT

The input and dump format follow the same pattern, some ASCII text specifying
the beginning of a record and then some binary data with some separators, and
a newline terminating the record, the format is:

	+key-length,value-length:KEY->VALUE
	+key-length,value-length:KEY->VALUE
	...
	+key-length,value-length:KEY->VALUE

Despite the presence of textual data, the input key and value can contain
binary data, including the ASCII NUL character.

An example, encoding the key value pair "abc" to "def" and "G" to "hello":

	+3,3:abc->def
	+1,5:G->hello

The following [awk][] script can be used to pre-process a series of key-value
pairs in the format "key value", with one record per line and optional comment
lines:

	#!/bin/sh
	awk '
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

A word consists of a 4-byte/32-bit value (although this may be changed via
compile time options, creating an incompatible format). All word values are
stored in little-endian format.

The initial hash table contains an array of 256 2-word values.
The words are; a position of a hash table in the file and the number of buckets
in that hash table, stored in that order. To lookup a key the key is first
hashed, the lowest eight bits of the hash are used to index into the initial table
and if there are values in this hash the search then proceeds to the second hash
table at the end of the file.

The hash tables at the end of the file contains an array of two word records,
containing the full hash and a file position of the key-value pair. To search
for a key in this table the hash of the key is taken and the lowest eight bits
are discarded by shifting right eight places, the hash is then taken modulo the
number of elements in the hash table, the resulting value is used as an initial
index into the hash table. Searching continues until the key is found, or an
empty record is found, or the number of records in the table have been searched
through with no match. A key is compared by looking at the hash table records,
if the hash of the key matches the stored hash in the hash table records then a
possible match is found, the file position is then used to look up the
key-value pair and the key is compared.

The number of buckets in the hash table is chosen as twice the number of
populated entries in the hash table.

A key-value pair is stored as two words containing the key length and the value
length in that order, then the key, and finally the value.

The hashing algorithm used is similar to [djb2][], but with a minor
modification that an exclusive or replaces an addition. The algorithm calculates
hashes of the size of a word, the initial hash value is the special number '5381'.
The hash is calculated as the current hash value multiplied by 33, to which the
new byte to be hashes and the result of multiplication under go an exclusive or
operation. This repeats until all bytes to be hashed are processed. All
arithmetic operations are unsigned and performed modulo 2 raised to the power
of 32.

The pseudo code for this is:

	set HASH to 5381
	for each OCTET in INPUT:
		set HASH to: ((HASH * 33) % pow(2, 32)) xor OCTET
	return HASH

Note that there is nothing in the file format that disallows duplicate keys in
the database, in fact the API allows duplicate keys to be retrieved. Both key
and data values can also be zero bytes long. There are also no special
alignment requirements on the data.

The best documentation on the file format is a small pure python script that
implements a set of functions for manipulating a CDB database, a description is
available here <http://www.unixuser.org/~euske/doc/cdbinternals/> and the
script itself is available at the bottom of that page
<http://www.unixuser.org/~euske/doc/cdbinternals/pycdb.py>.

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

# CDB C API

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
provide a series of callbacks. The callbacks are simple to implement on a
hosted system, examples are provided in [main.c][] in the project repository,
but this means the library is not just read to use.

There are two sets of operations that most users will want to perform; creating
a database and reading keys. After the callbacks have been provided, to create
a database requires opening up a new database in create mode:

	/* error handling omitted for brevity */
	cdb_t *cdb = NULL;
	cdb_file_operators_t ops = { /* Your file callbacks go here */ };
	cdb_open(&cdb, &ops, NULL, 1, "example.cdb");
	cdb_buffer_t key   = { .length = 5, .buffer = "hello" };
	cdb_buffer_t value = { .length = 5, .buffer = "world" };
	cdb_add(cdb, &key, &value);
	cdb_close(cdb);

If you are dealing with mostly NUL terminated ASCII/UTF-8 strings it is worth
creating a function to deal with this:

	int cdb_add_string(cdb_t *cdb, const char *key, const char *value) {
		assert(cdb);
		assert(key);
		assert(value);
		const cdb_buffer_t k = { .length = strlen(key),   .buffer = (char*)key   };
		const cdb_buffer_t v = { .length = strlen(value), .buffer = (char*)value };
		return cdb_add(cdb, &k, &v);
	}

Note that you *cannot* query for a key from a database opened up in create
mode and you *cannot* add a key-value pair to a database opened up in read
mode. The operations are mutually exclusive.

To search for a key within the database, you open up a database connection in
read mode (create = 0):

	/* error handling omitted for brevity */
	cdb_t *cdb = NULL;
	cdb_file_operators_t ops = { /* Your file callbacks go here */ };
	cdb_open(&cdb, &ops, NULL, 1, "example.cdb");
	cdb_buffer_t key = { .length = 5, .buffer = "hello" };
	cdb_file_pos_t value = { 0, 0 };
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
negative number is to call 'cdb\_get\_error' and then 'cdb\_close' and never
use the handle again. 'cdb\_get\_error' must not be used on a closed handle.

As there are potentially duplicate keys, the function 'cdb\_get\_count' can be
used to query for duplicates. It sets the parameter count to the number of
records found for that key (and it sets count to zero, and returns zero, if no
keys are found, it returns one if one or more keys were found).

The function 'cdb\_get\_error' can be used to query what error has occurred, if
any. On an error a negative value is returned, the meaning of this value is
deliberately not included in the header as the errors recorded and the
meaning of their values may change. Use the source for the library to determine
what error occurred.

The function 'cdb\_get\_version' returns the version number in an out parameter 
and information about the compile time options selected when the library was built. 
A [Semantic Version Number][] is used, which takes the form "MAJOR.MINOR.PATCH".
The PATCH number is stored in the Least Significant Byte, the MINOR number the
next byte up, and the MAJOR in the third byte. The fourth byte contains the
compile time options.

There are several things that could be done to speed up the database but this
would complicate the implementation and the API.

# BUILD REQUIREMENTS

If you are building the program from the repository at
<https://github.com/howerj/cdb> you will need [GNU Make][] and a [C Compiler][].
The library is written in pure [C99][] and should be fairly simple
to port to another platform. Other [Make][] implementations may work, however
they have not been tested. [git][] is also used as part of the build system.

First clone the repository and change directory to the newly clone repository:

	git clone https://github.com/howerj/cdb cdb
	cd cdb

Type 'make' to build the *cdb* executable and library.

Type 'make test' to build and run the *cdb* internal tests. The script called
't', written in [sh][], does more testing, and tests that the user interface is
working correctly. 'make dist' is used to create a compressed tar file for
distribution. 'make install' can be used to install the binaries, however the
default installation directory (which can be set with the 'DESTDIR' makefile
variable) installs to a directory called 'install' within the repository - it
will not actually install anything. Changing 'DESTDIR' to '/usr' should install
everything properly. [pandoc][] is required to build the manual page for
installation, which is generated from this [markdown][] file.

Look at the source file [cdb.c][] to see what compile time options can be
passed to the compiler to enable and disable features (if code size is a
concern then the ability to create databases can be removed, for example).

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
  information about the file type and structure.

TODO:
* [ ] Reduce data structure sizes needed
* [ ] Bench mark this library against other CDB libraries, some ideas for
increasing performance include; memory mapping files, reducing system calls,
buffering file streams, but most importantly - benchmark the code against 
other CDB implementations.
* [ ] Document a possible file format/header format based on PNG specification,
  document design decisions and improve documentation
* [ ] Cleanup/Simplify C API and make multiple key retrieval more efficient
* [ ] Remove TODOs before merging 4.0 branch onto master, squashing commits if
  needed.
* [ ] Remove the pre-processor typedefs if possible and simplify header,
  removed unneeded or ugly header functions
* [ ] Change other projects to incorporate the lessons learned in this one
  in terms of project organization, how the makefile is structured, the C API,
  and more.
* [ ] Generate ctags and hide in '.git' folder. Include makefile itself in
  list of dependencies.
* [ ] Make a script for benchmarking this implementation against others, if
  possible including memory usage, program size, and of course speed.
* [ ] An option for dumping out keys and their hashes could be made, using
  Unix utilities it would then be possible to construct the database in hash
  value order.
* [ ] If using CDB purely to test membership of a set then this DB places
  a 4 byte overhead per key which is unneeded. This could be made into an
  option.
* [x] Allow custom compare and hash functions in the database, such as
  SOUNDEX, so similar keys *collide* for a fuzzy search (a second index 
  could be used with the original hash algorithm so we can perform an
  exact lookup as well). Different indices could be used by specifying
  a different offset for the initial hash table.

# BUGS

For any bugs, email the [author][]. It comes with a 'works on my machine
guarantee'. The code has been written with the intention of being portable, and
should work on 32-bit and 64-bit machines. It is tested more frequently on a
64-bit Linux machine, and less frequently on Windows. Please give a
detailed bug report (including but not limited to what machine/OS you are
running on, compiler, compiler version, a failing example test case, etcetera).

# COPYRIGHT

The libraries, documentation, and the test driver program are licensed under
the [Unlicense][]. Do what thou wilt.

[author]: howe.r.j.89@gmail.com
[main.c]: main.c
[cdb.c]: cdb.c
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
