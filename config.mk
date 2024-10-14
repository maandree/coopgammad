PREFIX = /usr
MANPREFIX = $(PREFIX)/share/man

# For class of preserved clut
PKGNAME = coopgammad
COMMAND = coopgammad

CC = cc -std=c11

CPPFLAGS = -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_GNU_SOURCE -DUSE_VALGRIND
CFLAGS   = -Wall
LDFLAGS  = -lgamma
