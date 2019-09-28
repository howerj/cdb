% cdb(1) | Constant Database v1.00

# NAME

CDB - An interface to the Constant Database Library

# SYNOPSES

cdb -h

cdb -\[rcdkstV\] file.cdb

cdb -q file.cdb key \[record#\]

# DESCRIPTION

	Author:     Richard James Howe
	License:    Unlicense
	Repository: <https://github.com/howerj/cdb>
	Email:      howe.r.j.89@gmail.com

A clone of the [CDB][] database, a simple, read-only (once created) database.
The database library is designed so it can be embedded into a microcontroller 
if needed. This program can be used for creating and querying CDB databases,
which are key-value pairs of binary data.

# OPTIONS

**-h** : print out this help message and exit successfully

**-t** *file.cdb* : run internal tests, exit with zero on a pass

**-c**  *file.cdb* : run in create mode

**-d**  *file.cdb* : dump the database

**-k**  *file.cdb* : dump the keys in the database

**-s**  *file.cdb* : print statistics about the database

**-V**  *file.cdb* : validate database

**-r**  *file.cdb* : read keys in a read-query-print loop

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
returned if a key is not found.

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

# FILE FORMAT

The file format is incredibly simple, it is designed so that only the header
and the hash table index needs to be stored in memory during generation of the
table - the keys and values can be streamed on to the disk. The header consists
of 256 2-word values forming an initial hash table that point to the hash
tables at the end of the file, the key-value records, and then up to 256 hash 
tables pointing to the key-value pairs.

A word consists of a 4-byte/32-bit value (although this may be changed via
compile time options, creating an incompatible format). All word values are
stored in little-endian format.

The initial hash table contains an array of 256 2-word values, as mentioned.
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
operation. This repeats until all bytes to be hashed are processed.

Note that there is nothing in the file format that disallows duplicate keys in
the database, in fact the API allows duplicate keys to be retrieved. Both key
and data values can also be zero bytes long.

The best documentation on the file format is a small pure python script that
implements a set of functions for manipulating a CDB database, a description is
available here <http://www.unixuser.org/~euske/doc/cdbinternals/> and the
script itself is available at the bottom of that page
<http://www.unixuser.org/~euske/doc/cdbinternals/pycdb.py>.

	         Constant Database Sections
	.-------------------------------------------.
	|   256 Bucket Initial Hash Table (2KiB)    |
	.-------------------------------------------.
	|            Key Value Pairs                |
	.-------------------------------------------.
	|       0-256 Secondary Hash Tables         |
	.-------------------------------------------.

	    256 Bucket Initial Hash Table (2KiB)
	.-------------------------------------------.
	| { P, L } | { P, L } | { P, L } |   ...    |
	.----------+----------+----------+----------.
	|   ...    | { P, L } | { P, L } | { P, L } |
	.-------------------------------------------.
	P = Position of secondary hash table
	L = Number of buckets in secondary hash table

	.-------------------------------------------.
	| { KL, VL } | KEY ...      | VALUE ...     |
	.-------------------------------------------.
	KL    = Key Length
	VL    = Value Length
	KEY   = Varible length binary data key
	VALUE = Variable length binary value

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

# CDB C API

There are a few goals that the API has:

* It is simple, there should be few functions and data structures.
* The API is fairly easy to use.
* There should be minimal dependencies on the C standard library. The
  library itself should be small and not be a huge, non-portable, "optimized",
  mess.
* The user should decide when, where and how allocations are performed. The
  working set should be small.
* The database driver should be somewhat tolerant to erroneous files.

Some of these goals are in conflict, being able to control allocations and
having minimal dependencies allow the library to be used in an embedded system,
however it means that in order to do very basic things the user has to
provide a series of callbacks. The callbacks are simple to implement on a
hosted system, examples are provided in [main.c][] in the project repository.

# BUILD REQUIREMENTS

If you are building the program from the repository at
<https://github.com/howerj/cdb> you will need [GNU Make][] and a 
[C Compiler][]. The library is written in pure [C99][] and
should be fairly simple to port to another platform. Other [Make][]
implementations may work, however they have not been tested.

Type 'make' to build the *cdb* executable and library.

Type 'make test' to build and run the *cdb* internal tests. The script called
't', written in [sh][], does more testing, and tests that the user interface is
working correctly.

# POSSIBLE DIRECTIONS

The wish list contains a list of ideas that may be cool to implemented, but
probably will not be, or can be implemented on top of the program anyway.

- One way of improving performance would be to keep the first, initial, hash
  table in memory. This is 2KiB, however this conflicts with a goal of minimal
  memory usage. In fact all of the indexes could be stored in memory, speeding
  up the search. One, or both, levels could be enabled by a compile time
  option.
- Adding a header along with a [CRC][] for error detection, lessons learned
  from other file formats can be incorporated, some guides are available at:
  - <https://softwareengineering.stackexchange.com/questions/171201>
  - <https://stackoverflow.com/questions/323604>
  - And the PNG Specification: <https://www.w3.org/TR/PNG/>
  This would make the format incompatible with other programs that
  manipulate the CDB file format however. But would allow the shrinking
  of empty CDB databases, and to encode information about 
- (De)Compression could be added with the [shrink][] library, making the
  database smaller.
- Various schemas, type information, and a query language could be built upon
  this library.
- The user could supply their own hash and compare functions if fuzzy matching
  over the database is wanted, however they may have to force values into
  specific buckets to support this.
- Integrate with utilities like [littlefs][] or other embedded file systems.
- A few functions could be added to allow the database to be updated after it
  has been created, obviously this would be fraught with danger, but it is
  possible to extend the database after creation despite the name.
- Instead of having two separate structures for the allocator and the file
  operations, the structure could be merged.

# BUGS

For any bugs, email the [author][]. It comes with a 'works on my machine
guarantee'. The code has been written with the intention of being portable, and
should work on 32-bit and 64-bit machines. It is tested more frequently on a
64-bit Linux machine, and less so on frequently on Windows. Please give a
detailed bug report (including but not limited to what machine/OS you are 
running on, a failing example test case, etcetera).

- [ ] Improve code quality
  - [ ] Add assertions wherever possible
  - [ ] Stress test (attempt creation >4GiB DBs, overflow conditions, etcetera)
  - [ ] Validate data read off disk; lowest 8 bits of stored hash match bucket,
    pointers are within the right section, and refuse to read/seek if invalid. 
  - [ ] Use less memory when holding index in memory
- [ ] Improve error reporting
- [x] Turn into library
  - [ ] Settle on an API
  - [ ] add cdb\_read, cdb\_seek, ...
- [ ] Allow compile time customization of library
  - [ ] 16/32/64 bit version of the database
  - [ ] configurable endianess
- [x] Document format and command line options.
  - [x] Add ASCII diagrams to describe format
  - [ ] Improve prose of format description
  - [x] Generate manual page from this 'readme.md' by either using
  [ronn][] or [pandoc][]
  - [ ] Remove this list once it is complete
- [ ] Benchmark the system.
  - [ ] Adding timing information to operations.
  - [ ] Possible improvements include larger I/O buffering and avoiding
    scanf/printf functions (using custom numeric routines instead).
  - [ ] Different hash functions could improve performance

# COPYRIGHT

The libraries, documentation, and the program at licensed under the 
[Unlicense][]. Do what thou wilt.

[author]: howe.r.j.89@gmail.com
[main.c]: main.c
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
