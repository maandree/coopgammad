PREFIX = /usr
MANPREFIX = $(PREFIX)/share/man

# For class of preserved clut
PKGNAME = coopgammad
COMMAND = coopgammad

CC=cc

CPPFLAGS = -D_XOPEN_SOURCE=700 -D_GNU_SOURCE -DUSE_VALGRIND
CFLAGS   = -std=c11 -Wall -Og
LDFLAGS  = -lgamma -s
