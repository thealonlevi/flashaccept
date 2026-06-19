# Makefile for flashaccept — a fast io_uring TCP request/reply acceptor.
#
# Targets:
#   make            build the static and shared libraries (default)
#   make examples   build the example program(s) against the library
#   make clean      remove all build artifacts

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
CFLAGS  += -Iinclude -fPIC
LDLIBS  := -luring -lpthread

SRC      := src/flashaccept.c
OBJ      := $(SRC:.c=.o)

STATIC   := libflashaccept.a
SHARED   := libflashaccept.so

EXAMPLES := examples/echo_server

.PHONY: all examples clean

all: $(STATIC) $(SHARED)

# Static library.
$(STATIC): $(OBJ)
	$(AR) rcs $@ $^

# Shared library.
$(SHARED): $(OBJ)
	$(CC) -shared -o $@ $^ $(LDLIBS)

# Object files.
%.o: %.c include/flashaccept.h
	$(CC) $(CFLAGS) -c -o $@ $<

# Examples link against the static library for a self-contained binary.
examples: $(EXAMPLES)

examples/echo_server: examples/echo_server.c $(STATIC)
	$(CC) $(CFLAGS) -o $@ examples/echo_server.c $(STATIC) $(LDLIBS)

clean:
	rm -f $(OBJ) $(STATIC) $(SHARED) $(EXAMPLES)
