# Makefile - mywc built into build/glibc/ or build/musl/.
#   make                build with gcc and glibc
#   make LIBC=musl      build with musl-gcc, statically linked
#   make clean          remove build/

LIBC  ?= glibc
BUILD := build/$(LIBC)

ifeq ($(LIBC),musl)
  CC      := musl-gcc
  LDFLAGS += -static
else ifneq ($(LIBC),glibc)
  $(error LIBC must be glibc or musl, not '$(LIBC)')
endif

CPPFLAGS += -MMD -MP
CFLAGS   ?= -O2
CFLAGS   += -std=c99 -Wall -Wextra -pedantic
OBJS     := $(BUILD)/main.o $(BUILD)/count.o

.PHONY: all clean

all: $(BUILD)/mywc

$(BUILD)/mywc: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $@

clean:
	rm -rf build

-include $(wildcard $(BUILD)/*.d)
