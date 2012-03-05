EXTENSION    = pg_journal
EXTVERSION   = $(shell grep default_version $(EXTENSION).control | sed -e "s/default_version[[:space:]]*=[[:space:]]*'\([^']*\)'/\1/")

DATA         = $(filter-out $(wildcard sql/*--*.sql),$(wildcard sql/*.sql))
DOCS         = $(wildcard doc/*.md)
TESTS        = $(wildcard test/sql/*.sql)
REGRESS      = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --inputdir=test --load-language=plpgsql
MODULES      = $(patsubst %.c,%,$(wildcard src/*.c))
PG_CONFIG    = pg_config
PKG_CONFIG   = pkg-config
PG91         = $(shell $(PG_CONFIG) --version | grep -qE " 8\.| 9\.0" && echo no || echo yes)

PG_CPPFLAGS = $(shell $(PKG_CONFIG) libsystemd-journal --cflags)
SHLIB_LINK += -lsystemd-journal -lsystemd-id128

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# WTF! Makefile.shlib tells us to set SHLIB_LINK, but that has no effect
# Enough debugging this Makefile spaghetti, I'll just hard-code the command
src/pg_journal.so:: src/pg_journal.o
	gcc -O2 -Wall -Wmissing-prototypes -Wpointer-arith -Wdeclaration-after-statement -Wendif-labels -Wmissing-format-attribute -Wformat-security -fno-strict-aliasing -fwrapv -fexcess-precision=standard -g -fpic -L/usr/local/pgsql/lib -Wl,--as-needed -Wl,-rpath,'/usr/local/pgsql/lib',--enable-new-dtags  -shared -o $@ src/pg_journal.o $(SHLIB_LINK)

