TARGET_CC ?= /opt/amiga/bin/m68k-amigaos-gcc
HOST_CC ?= cc
TARGET := AmiEditor
SOURCES := src/main.c src/ui.c src/document.c src/fileio.c src/font.c src/tree.c src/syntax.c
OBJECTS := $(SOURCES:.c=.o)
DEPS := $(OBJECTS:.o=.d)
CPPFLAGS := -Iinclude
CFLAGS := -std=gnu99 -m68000 -msoft-float -noixemul -Os -Wall -Wextra -Wshadow -Wconversion -Wno-pointer-sign -MMD -MP
LDFLAGS := -m68000 -msoft-float -noixemul

.PHONY: all clean test
all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(TARGET_CC) $(LDFLAGS) -o $@ $(OBJECTS)

src/%.o: src/%.c
	$(TARGET_CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

test: build/test_syntax
	./build/test_syntax

build/test_syntax: src/syntax.c tests/test_syntax.c include/syntax.h
	mkdir -p build
	$(HOST_CC) -Iinclude -DSYNTAX_HOST_TEST -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/syntax.c tests/test_syntax.c

clean:
	rm -f $(TARGET) $(OBJECTS) $(DEPS) build/test_syntax

-include $(DEPS)
