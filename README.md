serve
=====

The `serve` utility is a generic server for POSIX systems.  It can run almost
any shell command and turn it into a server application.

The `serve` utility should conform to POSIX 2017.

Building
--------

Building `serve` is as easy as running `make`.


You can change the `CFLAGS` macro in the `Makefile` to adapt your build to your
C compiler.  The default should work on any implementation of the POSIX `c99`
utility.  On GCC, good options to add are:
* `-pedantic -Wall -Wextra -fanalyzer` for diagnostics;
* `-g -Og` for debugging;
* `-O3 -flto` for optimizations.

Don’t remove the `-D_POSIX_C_SOURCE=200809L` option: POSIX conformance needs
it.

Usage
-----

The manual page `serve.tr` documents the full usage.  On GNU/Linux, you can
read it with the `man -l serve.tr` command.

The simplest way to test `serve` is with the following commands in two
different terminals.

```bash
# Terminal 1
serve -a "inet 127.0.0.1 5000" cat

# Terminal 2
telnet -4 127.0.0.1 5000
```

Keep in mind `telnet` adds carriage returns before all line feeds.  This makes
it unsuitable to test nontrivial applications.

Things left to do
-----------------

* Implement socket types.

* Implement protocol specifications.

* Add a way to limit incoming connections below OS limit.
