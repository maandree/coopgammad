PREFIX = /usr
MANPREFIX = $(PREFIX)/share/man

# For class of preserved CLUT
PKGNAME = coopgammad
COMMAND = coopgammad

# The makefile uses $(CC) and therefore expects it to be a single command name.
# If you need a specific language mode, set it via CFLAGS.
CC = cc -std=c11

CPPFLAGS = -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_GNU_SOURCE -DUSE_VALGRIND
CFLAGS   = -Wall
LDFLAGS  = -lgamma
