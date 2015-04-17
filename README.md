Building pg\_journal
====================

This is a PostgreSQL preload module for sending log messages directly to the
systemd journal log.

This document explains how to build pg\_journal. For usage documentation and
more info, please see the `doc/pg_journal.md` file or [read the latest version
online](http://pgxn.org/dist/pg_journal/doc/pg_journal.html).

Prerequisites:

* PostgreSQL version 9.2+ (earlier versions supported with a server patch).
* systemd v38 or newer with libsystemd-journal installed.

Quick start
-----------

To build and install this extension, simply run:

    make
    sudo make install

Then add the following line to your `postgresql.conf`:

    shared_preload_libraries = 'pg_journal'

This change requires a restart of PostgreSQL.

Installation
------------

To use this module, you need PostgreSQL version 9.2 or later. To use with
earlier versions, you need to patch and build your server manually. The
necessary patch is included under `patches/logging-hooks.patch` (courtesy of
Martin Pihlak).

To build and install this extension, simply run:

    % make
    % sudo make install

If you run into problems with building, see [PostgreSQL wiki for
troubleshooting](https://wiki.postgresql.org/wiki/Extension_build_troubleshooting)

Once pg\_journal is installed, you can enable it in your configuration file.
Find the `postgresql.conf` file (usually in your PostgreSQL data directory) and
add the following line:

    shared_preload_libraries = 'pg_journal'

You need to restart your server for this to take effect. After that, log
messages are automatically sent to journal.

Copyright and License
---------------------

Copyright (c) 2012-2015 Marti Raudsepp

pg\_journal and all related files are available under [The PostgreSQL
License](http://www.opensource.org/licenses/PostgreSQL). See LICENSE file for
details.

