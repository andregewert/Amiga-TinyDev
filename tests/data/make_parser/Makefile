# Sample Makefile for the tests of make_parser
CC = vc
CFLAGS := -O2 -c99
OBJS = main.o util.o
export PREFIX ?= Work:

.PHONY: all clean

all: hello

hello: $(OBJS)
	$(CC) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< \
		-o $@

ifdef DEBUG
CFLAGS += -g
else
CFLAGS += -DNDEBUG
endif

define banner
	echo "Building $(1)"
endef

include config.mk
-include local.mk

clean:
	delete $(OBJS) hello

SRCS = $(wildcard *.c) $(patsubst %.o,%.c,$(OBJS))
