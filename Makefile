.POSIX:

CONFIGFILE = config.mk
include $(CONFIGFILE)

XCPPFLAGS = -D'PKGNAME="$(PKGNAME)"' -D'COMMAND="$(COMMAND)"'

PARTS =\
	communication\
	state\
	util\
	servers-master\
	servers-kernel\
	servers-crtc\
	servers-gamma\
	servers-coopgamma\
	types-filter\
	types-output\
	types-ramps\
	types-message\
	types-ring

OBJ = $(PARTS:=.o) coopgammad.c

HDR = $(PARTS:=.h) arg.h

all: coopgammad
$(OBJ): $(@:.o=.c) $(HDR)

.c.o:
	$(CC) -c -o $@ $< $(XCPPFLAGS) $(CPPFLAGS) $(CFLAGS)

coopgammad: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

install: coopgammad
	mkdir -p -- "$(DESTDIR)$(PREFIX)/bin"
	mkdir -p -- "$(DESTDIR)$(MANPREFIX)/man1"
	cp -- coopgammad "$(DESTDIR)$(PREFIX)/bin/coopgammad"
	cp -- coopgammad.1 "$(DESTDIR)$(MANPREFIX)/man1/coopgammad.1"

uninstall:
	-rm -f -- "$(DESTDIR)$(MANPREFIX)/man1/coopgammad.1"
	-rm -f -- "$(DESTDIR)$(PREFIX)/bin/coopgammad"

clean:
	-rm -rf -- coopgammad *.o *.su

.SUFFIXES:
.SUFFIXES: .o .c

.PHONY: all install uninstall clean
