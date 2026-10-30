# Makefile for parley (GNU make).
#
#   make                 build with glibc into build/glibc/
#   make LIBC=musl       build with musl (static) into build/musl/
#   make SANITIZE=1      build with sanitizers into build/glibc-san/
#   make test            build and run the unit tests, the fuzzer and the
#                        integration test
#   make test-all        run the tests with glibc and with musl
#   make release         optimized, stripped binaries in build/glibc-release/
#                        (make release LIBC=musl: build/musl-release/)
#   make install         install the release binaries into $(DESTDIR)$(PREFIX)/bin
#   make dist            the source tarball parley-VERSION.tar.gz
#   make bench           build the benchmark client, build/glibc/bench
#   make clean           remove build/ and the tarball

LIBC  ?= glibc
BUILD := build/$(LIBC)
PREFIX ?= /usr/local

# The version, read from src/version.h, so it is written in one place only.
VERSION := $(shell sed -n 's/^#define PARLEY_VERSION "\(.*\)"/\1/p' src/version.h)

ifeq ($(LIBC),musl)
  CC      := musl-gcc
  LDFLAGS += -static
else ifneq ($(LIBC),glibc)
  $(error LIBC must be glibc or musl, not '$(LIBC)')
endif

# -D_POSIX_C_SOURCE: ask every header for POSIX.1-2008 on top of C99.
# -MMD -MP: write build/*/foo.d files listing the headers foo.c includes.
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -MMD -MP
CFLAGS   ?= -O2 -g
CFLAGS   += -std=c99 -Wall -Wextra -pedantic -Werror

# Sanitizers add run-time checks for memory errors (ASan) and undefined
# behaviour (UBSan). Their run-time libraries need glibc; with musl we can
# only use UBSan in trap mode, which crashes on the spot without a message.
ifeq ($(SANITIZE),1)
  ifeq ($(LIBC),musl)
    SANFLAGS := -fsanitize=undefined -fsanitize-undefined-trap-on-error
  else
    SANFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer
  endif
  CFLAGS  += $(SANFLAGS)
  LDFLAGS += $(SANFLAGS)
  BUILD   := $(BUILD)-san
endif

# A release build: optimized, without debug information, and with the
# assert()s compiled out (-DNDEBUG makes assert() expand to nothing).
ifeq ($(RELEASE),1)
  CFLAGS   := -O2 -std=c99 -Wall -Wextra -pedantic -Werror
  CPPFLAGS += -DNDEBUG
  BUILD    := $(BUILD)-release
endif

SERVER_OBJS := $(addprefix $(BUILD)/, parleyd.o server.o room.o conn.o clock.o io.o net.o sig.o msg.o nick.o strbuf.o str.o log.o)
CLIENT_OBJS := $(addprefix $(BUILD)/, parley.o ui.o term.o io.o net.o sig.o msg.o strbuf.o str.o log.o)
TEST_OBJS   := $(addprefix $(BUILD)/, unit.o test_str.o test_nick.o test_strbuf.o \
                 test_msg.o test_io.o test_sig.o test_net.o \
                 io.o net.o sig.o msg.o nick.o strbuf.o str.o)
FUZZ_OBJS   := $(addprefix $(BUILD)/, fuzz.o msg.o strbuf.o str.o)
ITEST_OBJS  := $(addprefix $(BUILD)/, itest.o clock.o io.o net.o msg.o strbuf.o str.o)
BENCH_OBJS  := $(addprefix $(BUILD)/, bench.o clock.o io.o net.o msg.o strbuf.o str.o)

# Everything that goes into the source tarball.
DIST_FILES := Makefile README.md $(wildcard src/*.[ch] tests/*.[ch])

.PHONY: all test test-all release install dist bench clean

all: $(BUILD)/parleyd $(BUILD)/parley

$(BUILD)/parleyd: $(SERVER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/parley: $(CLIENT_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/unit: $(TEST_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/fuzz: $(FUZZ_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/itest: $(ITEST_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/bench: $(BENCH_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

bench: $(BUILD)/bench

test: all $(BUILD)/unit $(BUILD)/fuzz $(BUILD)/itest
	$(BUILD)/unit
	$(BUILD)/fuzz
	$(BUILD)/itest $(BUILD)/parleyd

# Run the whole test suite once per C library. Variables given on the
# command line (like SANITIZE=1) are passed on to the sub-makes.
test-all:
	$(MAKE) --no-print-directory test LIBC=glibc
	$(MAKE) --no-print-directory test LIBC=musl

# Build again with RELEASE=1, then strip the symbol tables: the binaries
# users run need no names of functions and variables.
release:
	$(MAKE) --no-print-directory all RELEASE=1
	strip $(BUILD)-release/parleyd $(BUILD)-release/parley

# DESTDIR lets packagers install into a staging directory.
install: release
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(BUILD)-release/parleyd $(BUILD)-release/parley $(DESTDIR)$(PREFIX)/bin/

# The tarball unpacks into parley-VERSION/.
dist:
	rm -rf build/parley-$(VERSION)
	mkdir -p build/parley-$(VERSION)/src build/parley-$(VERSION)/tests
	for f in $(DIST_FILES); do cp $$f build/parley-$(VERSION)/$$f || exit 1; done
	tar -C build -cf - parley-$(VERSION) | gzip -9 > parley-$(VERSION).tar.gz
	rm -rf build/parley-$(VERSION)

# Pattern rule: build/glibc/foo.o is made from src/foo.c. The build
# directory is an order-only prerequisite (after |): it must exist, but
# its timestamp doesn't make objects out of date.
$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

# Tests include the headers in src/ as "msg.h" etc., hence -Isrc.
$(BUILD)/%.o: tests/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $@

clean:
	rm -rf build parley-*.tar.gz

# The .d files made by -MMD: rebuild an object when a header it uses changes.
-include $(wildcard $(BUILD)/*.d)
