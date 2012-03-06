DOCS         = $(wildcard doc/*.md)
MODULES      = $(patsubst %.c,%,$(wildcard src/*.c))
PG_CONFIG    = pg_config
PKG_CONFIG   = pkg-config

PG_CPPFLAGS = $(shell $(PKG_CONFIG) libsystemd-journal --cflags)
SHLIB_LINK += $(shell $(PKG_CONFIG) libsystemd-journal --libs)

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# WTF! Makefile.shlib tells us to set SHLIB_LINK, but that has no effect
# Enough debugging this Makefile spaghetti, I'll just hard-code the command
src/pg_journal.so:: src/pg_journal.o
	gcc -O2 -Wall -Wmissing-prototypes -Wpointer-arith -Wdeclaration-after-statement -Wendif-labels -Wmissing-format-attribute -Wformat-security -fno-strict-aliasing -fwrapv -fexcess-precision=standard -g -fpic -L/usr/local/pgsql/lib -Wl,--as-needed -Wl,-rpath,'/usr/local/pgsql/lib',--enable-new-dtags  -shared -o $@ src/pg_journal.o $(SHLIB_LINK)

