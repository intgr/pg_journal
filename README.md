Building pg\_journal
====================

This is a PostgreSQL preload module for sending log messages directly to the
systemd journal log.

This document explains how to build pg\_journal. For usage documentation and
more info, please read the `doc/pg_journal.md` file.

Prerequisites:

* PostgreSQL version 9.2+ (earlier versions supported with a server patch).
* systemd v38 or newer with libsystemd-journal installed.

Installation
------------

To use this module, you need PostgreSQL version 9.2 or later. To use with
earlier versions, you need to patch and build your server manually. The
necessary patch is included under `patches/logging-hooks.patch` (courtesy of
Martin Pihlak).

You also need a recent enough version of systemd (v38+) with the journal feature
and library (`libsystemd-journal.so`) enabled.

After satisfying these requirements, just do this:

    make
    make install

If you encounter an error such as:

    "Makefile", line 8: Need an operator

You need to use GNU make, which may well be installed on your system as
`gmake`:

    gmake
    gmake install

If you encounter an error such as:

    make: pg_config: Command not found

Be sure that you have `pg_config` installed and in your path. If you used a
package management system such as RPM to install PostgreSQL, be sure that the
`-devel` package is also installed. If necessary tell the build process where
to find it:

    env PG_CONFIG=/path/to/pg_config make && make install

Once pg\_journal is installed, you can enable it in your configuration file.
Find the `postgresql.conf` file (usually in your PostgreSQL data directory) and
add the following line:

    shared_preload_libraries = 'pg_journal'

You need to restart your server for this to take effect. After that, log
messages are automatically sent to journal.

Copyright and License
---------------------

Copyright (c) 2012-2013 Marti Raudsepp

pg\_journal and all related files are available under [The PostgreSQL
License](http://www.opensource.org/licenses/PostgreSQL). See LICENSE file for
details.

