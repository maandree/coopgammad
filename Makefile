PREFIX = /usr
BINDIR = $(PREFIX)/bin
DATADIR = $(PREFIX)/share
MANDIR = $(DATADIR)/man
MAN1DIR = $(MANDIR)/man1
LICENSEDIR = $(DATADIR)/licenses

PKGNAME = coopgammad
COMMAND = coopgammad

KERNEL = $(shell uname | tr '[A-Z]_' '[a-z]-')

SRC = \
	coopgammad		\
	util			\
	communication		\
	state			\
	servers/master		\
	servers/kernel		\
	servers/crtc		\
	servers/gamma		\
	servers/coopgamma	\
	types/filter		\
	types/output		\
	types/ramps		\
	types/message		\
	types/ring

OPTIMISE = -O2

WARN = -Wall -Wextra -pedantic

CPP_linux = -DHAVE_LINUX_PROCFS
CPP_linux-libre = $(CPP_linux)

CCFLAGS = -std=c99 $(WARN) $(FFLAGS) $(OPTIMISE)
LDFLAGS = $(OPTIMISE) -lgamma
CPPFLAGS = -D'PKGNAME="$(PKGNAME)"' -D'COMMAND="$(COMMAND)"' -D_XOPEN_SOURCE=700 $(CPP_$(KERNEL))
ifdef USE_VALGRIND
CPPFLAGS += -DUSE_VALGRIND
endif



.PHONY: all
all: bin/coopgammad

.PHONY: base
base: cmd

.PHONY: cmd
cmd: bin/coopgammad

bin/coopgammad: $(foreach S,$(SRC),obj/$(S).o)
	@mkdir -p -- "$$(dirname -- "$@")"
	$(CC) $(LDFLAGS) -o $@ $^

obj/%.o: src/%.c src/*.h src/*/*.h
	@mkdir -p -- "$$(dirname -- "$@")"
	$(CC) $(CCFLAGS) $(CPPFLAGS) -c -o $@ $<



.PHONY: install
install: install-base install-doc

.PHONY: install-base
install-base: install-cmd install-copyright

.PHONY: install-copyright
install-copyright: install-license install-copying

.PHONY: install-doc
install-doc: install-man

.PHONY: install-cmd
install-cmd: bin/coopgammad
	mkdir -p -- "$(DESTDIR)$(BINDIR)"
	cp -- bin/coopgammad "$(DESTDIR)$(BINDIR)/$(COMMAND)"
	chmod 0755 -- "$(DESTDIR)$(BINDIR)/$(COMMAND)"

.PHONY: install-license
install-license:
	mkdir -p -- "$(DESTDIR)$(LICENSEDIR)/$(PKGNAME)"
	cp -- LICENSE "$(DESTDIR)$(LICENSEDIR)/$(PKGNAME)/LICENSE"
	chmod 0644 -- "$(DESTDIR)$(LICENSEDIR)/$(PKGNAME)/LICENSE"

.PHONY: install-copying
install-copying:
	mkdir -p -- "$(DESTDIR)$(LICENSEDIR)/$(PKGNAME)"
	cp -- COPYING "$(DESTDIR)$(LICENSEDIR)/$(PKGNAME)/COPYING"
	chmod 0644 -- "$(DESTDIR)$(LICENSEDIR)/$(PKGNAME)/COPYING"

.PHONY: install-man
install-man:
	mkdir -p -- "$(DESTDIR)$(MAN1DIR)"
	cp -- doc/coopgammad.1 "$(DESTDIR)$(MAN1DIR)/$(COMMAND).1"
	chmod 644 -- "$(DESTDIR)$(MAN1DIR)/$(COMMAND).1"



.PHONY: uninstall
uninstall:
	-rm -- "$(DESTDIR)$(MAN1DIR)/$(COMMAND).1"
	-rm -- "$(DESTDIR)$(LICENSEDIR)/$(PKGNAME)/COPYING"
	-rm -- "$(DESTDIR)$(LICENSEDIR)/$(PKGNAME)/LICENSE"
	-rmdir -- "$(DESTDIR)$(LICENSEDIR)/$(PKGNAME)"
	-rm -- "$(DESTDIR)$(BINDIR)/$(COMMAND)"



.PHONY: clean
clean:
	-rm -r bin obj

