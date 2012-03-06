pg\_journal
===========

Description
-----------

This is a PostgreSQL preload module for sending log messages directly to the
systemd journal log.

Configuration
-------------

Once pg\_journal is installed, you can enable it in your configuration file.
Find the `postgresql.conf` file (usually in your PostgreSQL data directory) and
add the following line:

    shared_preload_libaries = 'pg_journal'

You need to restart your server for this to take effect. After that, log
messages are automatically sent to journal. pg\_journal follows the usual
logging settings (`log_min_messages`, etc) to decide what to log.

If you want to prevent logged messages from appearing in the normal server log,
you can add the `skip_server_log` setting:

    custom_variable_classes = 'pg_journal' # PostgreSQL 9.1 and earlier only
    pg_journal.skip_server_log = on

Note that if journal logging fails for any reason, then pg\_journal falls back
to the server log despite this setting, allowing you to debug the issue.

Log fields
----------

pg\_journal adds the following log fields to log messages:

* `MESSAGE_ID`: only one message ID is defined,
  `a63699368b304b4cb51bce5644736306`, for `log_statement` log messages.
* `PRIORITY`: syslog priority level of message (number).
* `PGLEVEL`: PostgreSQL log level (number).
* `SQLSTATE`: SQL error code, [see PostgreSQL
  documentation](http://www.postgresql.org/docs/current/static/errcodes-appendix.html).
* `MESSAGE`: Primary log message.
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

Support
-------

  Bugs can be reported to pg\_journal's [GitHub issue
  tracker](https://github.com/intgr/pg_journal).

Author
------

Marti Raudsepp

Copyright and License
---------------------

Copyright (c) 2012 Marti Raudsepp

pg\_journal and all related files are available under [The PostgreSQL
License](http://www.opensource.org/licenses/PostgreSQL)

