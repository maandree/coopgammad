PREFIX = /usr
MANPREFIX = $(PREFIX)/share/man

# For class of preserved clut
PKGNAME = coopgammad
COMMAND = coopgammad

CPPFLAGS = -D_XOPEN_SOURCE=700 -DUSE_VALGRIND
#CFLAGS   = -std=c99 -Wall -O2
#LDFLAGS  = -lgamma -s
CFLAGS   = -std=c99 -Wall -Og -g
LDFLAGS  = -lgamma
