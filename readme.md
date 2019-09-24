# CDB Clone - Constant Database

**CURRENT PROJECT STATUS: UNTESTED WORK IN PROGRESS**

- Author: Richard James Howe
- License: Unlicense
- Repository: <https://github.com/howerj/cdb>
- Email: howe.r.j.89@gmail.com

A clone of the [CDB][] database, a simple, read-only (once created) database.
The database is designed so it can be embedded into a microcontroller if
needed.

# Build Requirements

[GNU Make][] and a [C Compiler][]. The library is written in pure [C99][] and
should be fairly simple to port to another platform.

Type 'make' to build the *cdb* executable and library.

Type 'make test' to build and run the *cdb* tests.

# Command Line Options

For a full list of command line options and a brief description of the program
you can build the program and then run the command './cdb -h'.

# CDB C API

There are a few goals that the API has:

* It is simple, there should be few functions and data structures.
* The user should decide when, where and how allocations are performed.
* The API is fairly easy to use.
* There should be minimal dependencies on the C standard library.

Some of these goals are in conflict, being able to control allocations and
having minimal dependencies allow the library to be used in an embedded system,
however it means that in order to do very basic things the user has to
provide a series of callbacks. The callbacks are simple to implement on a
hosted system, examples are provided in [main.c][].

# Input/Dump Format

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

# File Format

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
The words are; a position of a hash table in the file and the number of values
in that hash table, stored in that order. To lookup a key the key is first
hashed, the lowest eight bits of the hash are used to index into this table and
if there are values in this hash the search then proceeds to the second hash
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

# To Do List

- [x] Implement basic functionality in library.
  - [x] Implement database dump
  - [x] Implement key dump
  - [x] Implement key retrieval
  - [x] Implement multiple key retrieval
  - [x] Implement database write
- [ ] Make Unit tests, add assertions, make the system robust.
- [x] Turn into library
  - [ ] Settle on an API.
- [x] Allow allocation and file system operations to be user specified.
- [ ] Allow compile time customization of library; 32 or 64 bit, maybe even 16-bit. Endianess also.
- [x] Document format and command line options.
  - [ ] Mimic command line options for original program
- [x] Add options for dumping databases and collecting database statistics.
- [ ] Benchmark the system.
  - [ ] Adding timing information to operations.
  - [ ] Possible improvements include larger I/O buffering and avoiding
    scanf/printf functions (using custom numeric routines instead).
  - [ ] Different hash functions could improve performance

# Wish List

The wish list contains a list of ideas that may be cool to implemented, but
probably will not be, or can be implemented on top of the program anyway.

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

[main.c]: main.c
[CDB]: https://cr.yp.to/cdb.html
[GNU Make]: https://www.gnu.org/software/make/
[C Compiler]: https://gcc.gnu.org/
[C99]: https://en.wikipedia.org/wiki/C99
[littlefs]: https://github.com/ARMmbed/littlefs
[CRC]: https://en.wikipedia.org/wiki/Cyclic_redundancy_check
[shrink]: https://github.com/howerj/shrink
[djb2]: http://www.cse.yorku.ca/~oz/hash.html
