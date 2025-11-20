serve
=====

The `serve` utility is a generic server for POSIX systems.  It can run almost
any shell command and turn it into a server application.

When running `serve`, you specify a command to run in the session.  Each
incoming connection runs that command in a worker process.  Worker processes
have the following extra features:
* their standard input and output map to the connection socket;
* their standard error buffers into lines, and `serve` prints it on its
  standard output;
* the shell variable `$REMOTE` expands to a string representation of the
  incoming connection.

You can use the file system to share state between sessions, although this is
slow.  You should treat standard input and output as binary streams.  On POSIX
systems, this is trivial since all streams are binary.  But this constraint
gets in the way of porting `serve` to non-POSIX systems such as Windows.

The `serve` utility should conform to POSIX 2017.

Building
--------

Building `serve` is as easy as running `make`.

For more advanced builds, consult the manual.  You can do so with the following
commandsd.  They require [GNU Texinfo](https://www.gnu.org/software/texinfo/).

```bash
texi2any -D "VERSION 0.1" -D "UPDATED 2025-11-20" manual/serve.texi
info serve.info
```

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
