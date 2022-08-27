% cdb(1) | Constant Database

# NAME

CDB - An interface to the Constant Database Library

# SYNOPSES

cdb -h

cdb -\[cdkstV\] file.cdb

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
if needed. This program can be used for creating and querying CDB databases,
which consist of key-value pairs of binary data.

This program also includes several options that help in testing out the
database, one for hashing input keys and printing the hash for the default hash
function and another one for generating a database with (Pseudo-)random keys
and values of a given length.

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

**-g**  : spit out an example database

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
modification that an exclusive-or replaces an addition. The algorithm calculates
hashes of the size of a word, the initial hash value is the special number '5381'.
The hash is calculated as the current hash value multiplied by 33, to which the
new byte to be hashes and the result of multiplication under go an exclusive-or
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
	cdb_options_t ops = { /* Your file callbacks/options go here */ };
	cdb_open(&cdb, &ops, 1, "example.cdb");
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
	cdb_options_t ops = { /* Your file callbacks/options go here */ };
	cdb_open(&cdb, &ops, 1, "example.cdb");
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
what error occurred.

The function 'cdb\_version' returns the version number in an out parameter 
and information about the compile time options selected when the library was built. 
A [Semantic Version Number][] is used, which takes the form "MAJOR.MINOR.PATCH".
The PATCH number is stored in the Least Significant Byte, the MINOR number the
next byte up, and the MAJOR in the third byte. The fourth byte contains the
compile time options.

There are several things that could be done to speed up the database but this
would complicate the implementation and the API.

## EMBEDDED SUITABILITY

There are many libraries written in C, for better or worse, as it is the
lingua franca for software development at the moment. Few of those libraries
are directly suitable for use in [Embedded systems][] and are much less
flexible than they could be in general. Embedded systems pose some interesting
constraints (eschewing allocation via "malloc", lack of a file-system, and
more). By designing the library for an embedded system we can make a library
more useful not only for those systems but for hosted systems as well (eg. By
providing callbacks for the FILE functions we can redirect them to wherever
we like, the CDB file could be stored remotely and accessed via TCP, or it
could be stored locally using a normal file, or it could be stored in memory).

There are two sets of functions that should be abstracted out in nearly
every library, memory allocation (or even better, the caller can pass in
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
present. For good reason these test suites might need to be removed from builds
(as they might take up large amounts of space for code even if they are not
needed, which is at a premium in embedded systems with limited flash memory).

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
other function calls it.

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
* We could place the header within the key-value section of the database, or
  even at the end of the file.

Things that *should* be done, but have not:

* Fuzzing with [American Fuzzy Lop][] to iron out the most egregious
bugs, security relevant or otherwise. This has been used on the [pickle][]
library to great effect and it finds bugs that would not be caught be unit
testing alone.
* The current library implements a system for looking up data
stored to disk, a *system* could be created that does so much more.
Amongst the things that could be done are:
  - Using the CDB file format only as a serialization format
  for an in memory database which would allow key deletion/replacing.
  - Implementing the [memcached protocol][] to allow remote querying
  of data.
There are a few implementation strategies for doing this.
* Alternatively, just a simple Key-Value store that uses this database
as a back-end without anything else fancy.
* Better separation of host code from [main.c][] so it can be
reused.
* Changing the library interface so it is a [header only][] C library.
* Making a set of callbacks to allow an in memory CDB database, useful
for embedding the database within binaries.
* Designing a suite of benchmarks for similar databases and implementations
of CDB, much like <https://docs.huihoo.com/qdbm/benchmark.pdf>.

Porting this to Rust and making a crate for it would be nice,
[although implementations already exists](https://crates.io/search?q=cdb). 
Just making bindings for this library would be a good initial step, along
with other languages.

# BUGS

For any bugs, email the [author][]. It comes with a 'works on my machine
guarantee'. The code has been written with the intention of being portable,
and should work on 32-bit and 64-bit machines. It is tested more frequently
on a 64-bit Linux machine, and less frequently on Windows. Please give a
detailed bug report (including but not limited to what machine/OS you are
running on, compiler, compiler version, a failing example test case, etcetera).

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
