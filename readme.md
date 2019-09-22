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

# To Do List

- [ ] Implement basic functionality in library.
- [ ] Make Unit tests, add assertions, make the system robust.
- [ ] Turn into library and settle on an API.
- [ ] Allow allocation and file system operations to be user specified.
- [ ] Allow compile time customization of library; 32 or 64 bit, maybe even 16-bit.
- [ ] Document format and command line options.
- [ ] Allow 'Caveat Emptor' insertions into the database.
- [ ] Add a header to file format?
- [ ] Add options for dumping databases and collecting database statistics.
- [ ] Experiment with different hashes, file formats, database operators, etcetera.
- [ ] Generate manual page from the help option?

[main.c]: main.c
[CDB]: https://cr.yp.to/cdb.html
[GNU Make]: https://www.gnu.org/software/make/
[C Compiler]: https://gcc.gnu.org/
[C99]: https://en.wikipedia.org/wiki/C99
