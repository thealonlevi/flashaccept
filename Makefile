# Makefile for flashaccept — a fast io_uring TCP request/reply acceptor.
#
#   make             build static + shared libraries (default)
#   make examples    build the example program(s)
#   make install     install headers, libs, and pkg-config (PREFIX=/usr/local)
#   make uninstall   remove installed files
#   make clean       remove build artifacts
#
# Override PREFIX/DESTDIR for packaging, e.g. `make install PREFIX=/usr DESTDIR=pkgroot`.

VERSION  := 1.0.0
SOVER    := 1

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
CFLAGS  += -Iinclude -fPIC
LDFLAGS ?=                       # e.g. LDFLAGS=-L/opt/liburing/lib to link a custom liburing
LDLIBS  := -luring -lpthread

PREFIX  ?= /usr/local
LIBDIR  ?= $(PREFIX)/lib
INCDIR  ?= $(PREFIX)/include
PCDIR   ?= $(LIBDIR)/pkgconfig

SRC      := src/flashaccept.c
OBJ      := $(SRC:.c=.o)

STATIC   := libflashaccept.a
SHARED   := libflashaccept.so
SHARED_V := $(SHARED).$(VERSION)     # libflashaccept.so.1.0.0
SONAME   := $(SHARED).$(SOVER)       # libflashaccept.so.1

EXAMPLES := examples/echo_server

.PHONY: all examples clean install uninstall flashaccept.pc

all: $(STATIC) $(SHARED)

# Static library.
$(STATIC): $(OBJ)
	$(AR) rcs $@ $^

# Shared library with a proper SONAME, plus the usual dev/runtime symlinks:
#   libflashaccept.so -> libflashaccept.so.1 -> libflashaccept.so.1.0.0
$(SHARED): $(OBJ)
	$(CC) -shared -Wl,-soname,$(SONAME) -o $(SHARED_V) $^ $(LDFLAGS) $(LDLIBS)
	ln -sf $(SHARED_V) $(SONAME)
	ln -sf $(SONAME) $(SHARED)

%.o: %.c include/flashaccept.h
	$(CC) $(CFLAGS) -c -o $@ $<

examples: $(EXAMPLES)
examples/echo_server: examples/echo_server.c $(STATIC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ examples/echo_server.c $(STATIC) $(LDLIBS)

# pkg-config file (generated so prefix/version stay in sync).
flashaccept.pc:
	@printf 'prefix=%s\nexec_prefix=$${prefix}\nlibdir=%s\nincludedir=%s\n\n' "$(PREFIX)" "$(LIBDIR)" "$(INCDIR)" > $@
	@printf 'Name: flashaccept\nDescription: Fast io_uring TCP accept engine for Linux\nVersion: %s\n' "$(VERSION)" >> $@
	@printf 'URL: https://github.com/thealonlevi/flashaccept\n' >> $@
	@printf 'Libs: -L$${libdir} -lflashaccept -luring -lpthread\nCflags: -I$${includedir}\n' >> $@

install: all flashaccept.pc
	install -d $(DESTDIR)$(INCDIR) $(DESTDIR)$(LIBDIR) $(DESTDIR)$(PCDIR)
	install -m 644 include/flashaccept.h $(DESTDIR)$(INCDIR)/
	install -m 644 $(STATIC) $(DESTDIR)$(LIBDIR)/
	install -m 755 $(SHARED_V) $(DESTDIR)$(LIBDIR)/
	ln -sf $(SHARED_V) $(DESTDIR)$(LIBDIR)/$(SONAME)
	ln -sf $(SONAME) $(DESTDIR)$(LIBDIR)/$(SHARED)
	install -m 644 flashaccept.pc $(DESTDIR)$(PCDIR)/
	-ldconfig 2>/dev/null || true
	@echo "installed flashaccept $(VERSION) to $(DESTDIR)$(PREFIX)"

uninstall:
	rm -f $(DESTDIR)$(INCDIR)/flashaccept.h
	rm -f $(DESTDIR)$(LIBDIR)/$(STATIC) $(DESTDIR)$(LIBDIR)/$(SHARED) \
	      $(DESTDIR)$(LIBDIR)/$(SONAME) $(DESTDIR)$(LIBDIR)/$(SHARED_V)
	rm -f $(DESTDIR)$(PCDIR)/flashaccept.pc

clean:
	rm -f $(OBJ) $(STATIC) $(SHARED) $(SHARED_V) $(SONAME) $(EXAMPLES) flashaccept.pc
