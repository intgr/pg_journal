<!-- vim: set syn=markdown : -->
pg\_journal usage
=================

Description
-----------

This is a PostgreSQL preload module for sending log messages directly to the
systemd journal log.

Prerequisites:

* PostgreSQL version 9.2+ (earlier versions supported with a server patch).
* systemd v38 or newer with libsystemd-journal installed.

Configuration
-------------

Once pg\_journal is installed, you can enable it in the configuration file (see
README.md for installation instructions). Find the `postgresql.conf` file
(usually in your PostgreSQL data directory) and add the following line:

    shared_preload_libaries = 'pg_journal'

You need to restart your server for this to take effect. After that, log
messages are automatically sent to journal. pg\_journal follows the usual
logging settings (`log_min_messages`, etc) to decide what to log.

By default, enabling pg\_journal prevents log messages from appearing in other
log destinations (log file, syslog and csvlog). If you want to duplicate
messages to other log destinations too, you can to add the
`passthrough_server_log` setting:

    custom_variable_classes = 'pg_journal' # PostgreSQL 9.1 and earlier only
    pg_journal.passthrough_server_log = on

Note that if journal logging fails for any reason, then pg\_journal falls back
to the server log despite this setting, allowing you to debug the issue.

Usage
-----

After making the configuration changes above and restarting, PostgreSQL should
automatically start logging to journal. You can use the following command to
see the latest log messages (like `tail -f`) from 'postgres' processes:

    % journalctl -f _COMM=postgres
    Mar 07 18:30:27 hostname postgres[16028]: LOG:  loaded library "pg_journal"
    Mar 07 18:30:27 hostname postgres[16030]: LOG:  database system was shut down at 2012-03-07 18:30:26 EET
    Mar 07 18:30:27 hostname postgres[16028]: LOG:  database system is ready to accept connections
    Mar 07 18:30:27 hostname postgres[16034]: LOG:  autovacuum launcher started

Note that unlike regular PostgreSQL logging, only the primary message of each
error is displayed -- without DETAIL or HINT lines. **This will probably change
in a future version.** Until then, you need to use journalctl in the verbose
mode, which displays all logged fields (documented below):

    % journalctl -f _COMM=postgres -o verbose
    [...]
    Wed, 07 Mar 2012 18:38:36 +0200 [...]
        MESSAGE=LOG:  checkpoints are occurring too frequently (2 seconds apart)
        PRIORITY=6
        PGLEVEL=15
        HINT=Consider increasing the configuration parameter "checkpoint_segments".
        CODE_FILE=checkpointer.c
        CODE_LINE=488
        CODE_FUNCTION=CheckpointerMain
        _TRANSPORT=journal
        [... other systemd-specific fields ...]
    Wed, 07 Mar 2012 18:38:37 +0200 [...]
        MESSAGE=ERROR:  canceling statement due to user request
        PRIORITY=4
        PGLEVEL=20
        SQLSTATE=57014
        STATEMENT=insert into foo select generate_series(1,10000000);
        CODE_FILE=postgres.c
        CODE_LINE=2914
        CODE_FUNCTION=ProcessInterrupts
        PGUSER=joe
        PGDATABASE=sixpack
        PGHOST=[local]
        PGAPPNAME=psql
        _TRANSPORT=journal
        [...]

Log fields
----------

pg\_journal adds the following log fields to log messages:

* `MESSAGE_ID`: Only one message ID is defined,
  `a63699368b304b4cb51bce5644736306`, for `log_statement` log messages.
* `MESSAGE`: Primary log message and severity.
* `PRIORITY`: Syslog priority level of message (number).
* `PGLEVEL`: PostgreSQL log level (number).
* `SQLSTATE`: SQL error code, [see PostgreSQL
  documentation] (http://www.postgresql.org/docs/current/static/errcodes-appendix.html).
* `DETAIL`: Log message detail.
* `HINT`: Log message hint.
* `QUERY`: Internal query.
* `CONTEXT`: Context where the error occurred.
* `STATEMENT`: Statement/query that caused this error.
* `CODE_FILE`: PostgreSQL source file name where this error was reported.
* `CODE_LINE`: PostgreSQL source line number where this error was reported.
* `CODE_FUNCTION`: PostgreSQL internal function name.
* `PGUSER`: User name of client.
* `PGDATABASE`: Database where the client was connected to.
* `PGHOST`: Hostname or host:port where the client connected from.
* `PGAPPNAME`: Value of the `application_name` variable.

Changelog
---------

0.1.1 (2012-09-12)

* The meaning of `skip_server_log` setting was inverted and renamed to `passthrough_server_log`
* Log message severity is now always included in MESSAGE for consistency with PostgreSQL logs
* Minor fixes and improvements

0.1.0 (2012-03-08)

* Initial release

Support
-------

  Bugs should be reported to pg\_journal's [GitHub issue
  tracker](https://github.com/intgr/pg_journal/issues).

Author
------

Marti Raudsepp

Copyright and License
---------------------

Copyright (c) 2012 Marti Raudsepp

pg\_journal and all related files are available under [The PostgreSQL
License](http://www.opensource.org/licenses/PostgreSQL)

