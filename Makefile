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

LSP_TARGET := $(BUILD_DIR)/TinyDev-LSP
LSP_SOURCES := src/lsp/lsp_main.c src/lsp/lsp_broker.c src/lsp/lsp_rexx.c src/lsp/lsp_parser_runner.c src/lsp/lsp_json.c
LSP_OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(LSP_SOURCES))
LSP_DEPS := $(LSP_OBJECTS:.o=.d)

PARSER_C_TARGET := $(BUILD_DIR)/parsers/c_parser

.PHONY: all clean test
all: $(TARGET) $(LSP_TARGET) $(PARSER_C_TARGET)

$(TARGET): $(OBJECTS)
	$(TARGET_CC) $(LDFLAGS) -o $@ $(OBJECTS)

$(LSP_TARGET): $(LSP_OBJECTS)
	$(TARGET_CC) $(LDFLAGS) -o $@ $(LSP_OBJECTS)

$(PARSER_C_TARGET): src/parsers/c_parser.c src/lsp/lsp_json.c include/c_parser.h include/lsp_json.h
	@mkdir -p $(BUILD_DIR)/parsers
	$(TARGET_CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ src/parsers/c_parser.c src/lsp/lsp_json.c

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(TARGET_CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

test: $(BUILD_DIR)/test_syntax $(BUILD_DIR)/test_lsp_json $(BUILD_DIR)/test_c_parser
	./$(BUILD_DIR)/test_syntax
	./$(BUILD_DIR)/test_lsp_json
	./$(BUILD_DIR)/test_c_parser

$(BUILD_DIR)/test_syntax: src/syntax.c tests/test_syntax.c include/syntax.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DSYNTAX_HOST_TEST -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/syntax.c tests/test_syntax.c

$(BUILD_DIR)/test_lsp_json: src/lsp/lsp_json.c tests/test_lsp_json.c include/lsp_json.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/lsp/lsp_json.c tests/test_lsp_json.c

$(BUILD_DIR)/test_c_parser: src/parsers/c_parser.c src/lsp/lsp_json.c tests/test_c_parser.c include/c_parser.h include/lsp_json.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DC_PARSER_NO_MAIN -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/parsers/c_parser.c src/lsp/lsp_json.c tests/test_c_parser.c

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS) $(LSP_DEPS)
