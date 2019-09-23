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

# File Format

# To Do List

- [ ] Implement basic functionality in library.
  - [x] Implement database dump
  - [x] Implement key dump
  - [x] Implement key retrieval
  - [ ] Implement multiple key retrieval
  - [ ] Implement database write
  - [ ] Deal with duplicate keys by allowing multiple records to be
  stored per key and retrieved (along with a count of the number of
  records with the same key).
- [ ] Make Unit tests, add assertions, make the system robust.
- [ ] Turn into library and settle on an API.
- [x] Allow allocation and file system operations to be user specified.
- [ ] Allow compile time customization of library; 32 or 64 bit, maybe even 16-bit.
- [ ] Document format and command line options.
- [ ] Allow 'Caveat Emptor' insertions into the database.
- [ ] Add options for dumping databases and collecting database statistics.
- [ ] Experiment with different hashes, file formats, database operators, etcetera.
- [ ] Generate manual page from the help option?
- [ ] Integrate with utilities like [littlefs][], or other, embedded file systems.

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
- The user could supply their own hash and compare functions if fuzzy matching
  over the database is wanted, however they may have to force values into
  specific buckets to support this.

[main.c]: main.c
[CDB]: https://cr.yp.to/cdb.html
[GNU Make]: https://www.gnu.org/software/make/
[C Compiler]: https://gcc.gnu.org/
[C99]: https://en.wikipedia.org/wiki/C99
[littlefs]: https://github.com/ARMmbed/littlefs
[CRC]: https://en.wikipedia.org/wiki/Cyclic_redundancy_check
[shrink]: https://github.com/howerj/shrink
