TARGET_CC ?= /opt/amiga/bin/m68k-amigaos-gcc
HOST_CC ?= cc
BUILD_DIR := build
TARGET := $(BUILD_DIR)/tinyDE
SOURCES := src/main.c src/ui.c src/document.c src/fileio.c src/tree.c src/syntax.c src/minimap.c
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)
CPPFLAGS := -Iinclude
CFLAGS := -std=gnu99 -m68000 -msoft-float -noixemul -Os -Wall -Wextra -Wshadow -Wconversion -Wno-pointer-sign -MMD -MP
LDFLAGS := -m68000 -msoft-float -noixemul

.PHONY: all clean test
all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(TARGET_CC) $(LDFLAGS) -o $@ $(OBJECTS)

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(BUILD_DIR)
	$(TARGET_CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

test: $(BUILD_DIR)/test_syntax
	./$(BUILD_DIR)/test_syntax

$(BUILD_DIR)/test_syntax: src/syntax.c tests/test_syntax.c include/syntax.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DSYNTAX_HOST_TEST -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/syntax.c tests/test_syntax.c

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
