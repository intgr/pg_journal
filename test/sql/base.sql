\set ECHO 0
BEGIN;
\i sql/pg_journal.sql
\set ECHO all

-- Tests goes here.

ROLLBACK;
