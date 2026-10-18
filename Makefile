TARGET_CC ?= /opt/amiga/bin/m68k-amigaos-gcc
HOST_CC ?= cc
BUILD_DIR := build
TARGET := $(BUILD_DIR)/TinyDev
SOURCES := src/main.c src/ui.c src/document.c src/fileio.c src/tree.c src/syntax.c src/minimap.c
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)
CPPFLAGS := -Iinclude
CFLAGS := -std=gnu99 -m68000 -msoft-float -noixemul -Os -Wall -Wextra -Wshadow -Wconversion -Wno-pointer-sign -MMD -MP
LDFLAGS := -m68000 -msoft-float -noixemul

SCOUT_TARGET := $(BUILD_DIR)/TinyDev-Scout
SCOUT_SOURCES := src/scout/scout_main.c src/scout/scout_broker.c src/scout/scout_rexx.c src/scout/scout_parser_runner.c src/scout/scout_json.c
SCOUT_OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SCOUT_SOURCES))
SCOUT_DEPS := $(SCOUT_OBJECTS:.o=.d)

PARSER_C_TARGET := $(BUILD_DIR)/parsers/c_parser

DEVKIT := amiga-devkit
ifeq ($(wildcard $(DEVKIT)/make/charcheck.mk),)
$(error $(DEVKIT) is missing: git submodule update --init)
endif
LATIN1 := $(wildcard src/*.c src/*/*.c include/*.h tests/*.c scripts/* tests-host/* LICENSE Makefile Makefile-Amiga-vbcc)

.PHONY: all clean test
all: charcheck $(TARGET) $(SCOUT_TARGET) $(PARSER_C_TARGET)

# Included after "all" so that charcheck does not become the default goal.
include $(DEVKIT)/make/charcheck.mk

$(TARGET): $(OBJECTS)
	$(TARGET_CC) $(LDFLAGS) -o $@ $(OBJECTS)

$(SCOUT_TARGET): $(SCOUT_OBJECTS)
	$(TARGET_CC) $(LDFLAGS) -o $@ $(SCOUT_OBJECTS)

$(PARSER_C_TARGET): src/parsers/c_parser.c src/scout/scout_json.c include/c_parser.h include/scout_json.h
	@mkdir -p $(BUILD_DIR)/parsers
	$(TARGET_CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ src/parsers/c_parser.c src/scout/scout_json.c

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(TARGET_CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

test: $(BUILD_DIR)/test_syntax $(BUILD_DIR)/test_scout_json $(BUILD_DIR)/test_c_parser
	./$(BUILD_DIR)/test_syntax
	./$(BUILD_DIR)/test_scout_json
	./$(BUILD_DIR)/test_c_parser

$(BUILD_DIR)/test_syntax: src/syntax.c tests/test_syntax.c include/syntax.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DSYNTAX_HOST_TEST -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/syntax.c tests/test_syntax.c

$(BUILD_DIR)/test_scout_json: src/scout/scout_json.c tests/test_scout_json.c include/scout_json.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/scout/scout_json.c tests/test_scout_json.c

$(BUILD_DIR)/test_c_parser: src/parsers/c_parser.c src/scout/scout_json.c tests/test_c_parser.c include/c_parser.h include/scout_json.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DC_PARSER_NO_MAIN -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/parsers/c_parser.c src/scout/scout_json.c tests/test_c_parser.c

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS) $(SCOUT_DEPS)
