DOCS         = $(wildcard doc/*.md)
OBJS         = $(patsubst %.c,%.o,$(wildcard src/*.c))
MODULE_big   = pg_journal
PG_CONFIG    = pg_config
PKG_CONFIG   = pkg-config

PG_CPPFLAGS = $(shell $(PKG_CONFIG) libsystemd --cflags)
SHLIB_LINK  = $(shell $(PKG_CONFIG) libsystemd --libs)

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

