# Makefile for parley (GNU make).
#
#   make                 build with glibc into build/glibc/
#   make LIBC=musl       build with musl (static) into build/musl/
#   make SANITIZE=1      build with sanitizers into build/glibc-san/
#   make test            build and run the unit tests
#   make test-all        run the tests with glibc and with musl
#   make clean           remove build/

LIBC  ?= glibc
BUILD := build/$(LIBC)

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

SERVER_OBJS := $(addprefix $(BUILD)/, parleyd.o room.o conn.o io.o sig.o msg.o nick.o strbuf.o str.o log.o)
CLIENT_OBJS := $(addprefix $(BUILD)/, parley.o msg.o strbuf.o str.o log.o)
TEST_OBJS   := $(addprefix $(BUILD)/, unit.o test_str.o test_nick.o test_strbuf.o \
                 test_msg.o test_io.o test_sig.o io.o sig.o msg.o nick.o strbuf.o str.o)

.PHONY: all test test-all clean

all: $(BUILD)/parleyd $(BUILD)/parley

$(BUILD)/parleyd: $(SERVER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/parley: $(CLIENT_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/unit: $(TEST_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test: all $(BUILD)/unit
	$(BUILD)/unit

# Run the whole test suite once per C library. Variables given on the
# command line (like SANITIZE=1) are passed on to the sub-makes.
test-all:
	$(MAKE) --no-print-directory test LIBC=glibc
	$(MAKE) --no-print-directory test LIBC=musl

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
	rm -rf build

# The .d files made by -MMD: rebuild an object when a header it uses changes.
-include $(wildcard $(BUILD)/*.d)
