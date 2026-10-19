TARGET_CC ?= /opt/amiga/bin/m68k-amigaos-gcc
HOST_CC ?= cc
# build/: everything produced on the way (objects, dependency files, .debug
# files with symbols, host test binaries). bin/: only the finished Amiga
# programs and classes, laid out as they are used on the Amiga.
BUILD_DIR := build
BIN_DIR := bin
TARGET := $(BIN_DIR)/TinyDev
SOURCES := src/main.c src/ui.c src/document.c src/fileio.c src/tree.c src/syntax.c src/minimap.c src/toolbar.c src/settings.c src/splash.c src/prefswin.c src/closeimage.c src/search.c src/find.c
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)
CPPFLAGS := -Iinclude
CFLAGS := -std=gnu99 -m68000 -msoft-float -noixemul -Os -Wall -Wextra -Wshadow -Wconversion -Wno-pointer-sign -MMD -MP
LDFLAGS := -m68000 -msoft-float -noixemul

SCOUT_TARGET := $(BIN_DIR)/TinyDev-Scout
SCOUT_SOURCES := src/scout/scout_main.c src/scout/scout_broker.c src/scout/scout_rexx.c src/scout/scout_parser_runner.c src/scout/scout_json.c
SCOUT_OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SCOUT_SOURCES))
SCOUT_DEPS := $(SCOUT_OBJECTS:.o=.d)

PARSER_C_TARGET := $(BIN_DIR)/parsers/c_parser

# Gadget classes (src/classes/<name>/), each a disk-based class library for
# SYS:Classes/Gadgets/. They are written to bin/Classes/Gadgets/ so that
# TinyDev finds them as PROGDIR:Classes/Gadgets/<name>.gadget when they are
# not installed. src/classes/common/classlib.c (the library frame) is linked
# first and compiled per class with its class_config.h.
AMIGA_PREFIX := $(patsubst %/bin/m68k-amigaos-gcc,%,$(TARGET_CC))
TARGET_STRIP ?= $(AMIGA_PREFIX)/bin/m68k-amigaos-strip
CLASS_DIR := $(BIN_DIR)/Classes/Gadgets
CLASS_OBJ_DIR := $(BUILD_DIR)/classes
CLASS_NAMES := dirtree minimap
CLASS_CFLAGS := -std=gnu99 -m68000 -msoft-float -Os -fno-common -fomit-frame-pointer -fno-toplevel-reorder \
    -Wall -Wextra -Wshadow -Wconversion -Wno-pointer-sign -MMD -MP -Iinclude -Isrc/classes/common
CLASS_LIBS := -L$(AMIGA_PREFIX)/m68k-amigaos/libnix/lib -Wl,--start-group -lgcc -lnix -lnix20 -Wl,--end-group
CLASS_GADGETS := $(foreach c,$(CLASS_NAMES),$(CLASS_DIR)/$(c).gadget)

# $(1): class name
define CLASS_RULES
$(1)_OBJECTS := $$(CLASS_OBJ_DIR)/$(1)/classlib.o \
    $$(patsubst src/classes/$(1)/%.c,$$(CLASS_OBJ_DIR)/$(1)/%.o,$$(wildcard src/classes/$(1)/*.c))
CLASS_DEPS += $$($(1)_OBJECTS:.o=.d)

$$(CLASS_OBJ_DIR)/$(1)/classlib.o: src/classes/common/classlib.c
	@mkdir -p $$(dir $$@)
	$$(TARGET_CC) $$(CLASS_CFLAGS) -Isrc/classes/$(1) -c -o $$@ $$<

$$(CLASS_OBJ_DIR)/$(1)/%.o: src/classes/$(1)/%.c
	@mkdir -p $$(dir $$@)
	$$(TARGET_CC) $$(CLASS_CFLAGS) -Isrc/classes/$(1) -c -o $$@ $$<

$$(CLASS_DIR)/$(1).gadget: $$($(1)_OBJECTS)
	@mkdir -p $$(dir $$@)
	$$(TARGET_CC) -m68000 -msoft-float -nostartfiles -nostdlib -o $$(CLASS_OBJ_DIR)/$(1)/$(1).gadget.debug $$($(1)_OBJECTS) $$(CLASS_LIBS)
	$$(TARGET_STRIP) -o $$@ $$(CLASS_OBJ_DIR)/$(1)/$(1).gadget.debug
endef

DEVKIT := amiga-devkit
ifeq ($(wildcard $(DEVKIT)/make/charcheck.mk),)
$(error $(DEVKIT) is missing: git submodule update --init)
endif
LATIN1 := $(wildcard src/*.c src/*/*.c include/*.h include/*/*.h src/classes/*/*.c src/classes/*/*.h sfd/*.sfd tests/*.c scripts/* tests-host/* LICENSE Makefile)

.PHONY: all clean test icons
all: charcheck $(TARGET) $(CLASS_GADGETS) $(SCOUT_TARGET) $(PARSER_C_TARGET)

# Included after "all" so that charcheck does not become the default goal.
include $(DEVKIT)/make/charcheck.mk

$(foreach c,$(CLASS_NAMES),$(eval $(call CLASS_RULES,$(c))))

$(TARGET): $(OBJECTS)
	@mkdir -p $(BIN_DIR)
	$(TARGET_CC) $(LDFLAGS) -Wl,-u,___stkinit -o $@ $(OBJECTS)

$(SCOUT_TARGET): $(SCOUT_OBJECTS)
	@mkdir -p $(BIN_DIR)
	$(TARGET_CC) $(LDFLAGS) -o $@ $(SCOUT_OBJECTS)

# Compiled and linked in one step, so without -MMD (the .d file would land
# in bin/); the prerequisites name the headers.
$(PARSER_C_TARGET): src/parsers/c_parser.c src/scout/scout_json.c include/c_parser.h include/scout_json.h
	@mkdir -p $(BIN_DIR)/parsers
	$(TARGET_CC) $(CPPFLAGS) $(filter-out -MMD -MP,$(CFLAGS)) $(LDFLAGS) -o $@ src/parsers/c_parser.c src/scout/scout_json.c

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(TARGET_CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

test: $(BUILD_DIR)/test_syntax $(BUILD_DIR)/test_search $(BUILD_DIR)/test_scout_json $(BUILD_DIR)/test_c_parser
	./$(BUILD_DIR)/test_syntax
	./$(BUILD_DIR)/test_search
	./$(BUILD_DIR)/test_scout_json
	./$(BUILD_DIR)/test_c_parser

$(BUILD_DIR)/test_syntax: src/syntax.c tests/test_syntax.c include/syntax.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DSYNTAX_HOST_TEST -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/syntax.c tests/test_syntax.c

$(BUILD_DIR)/test_search: src/search.c tests/test_search.c include/search.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/search.c tests/test_search.c

$(BUILD_DIR)/test_scout_json: src/scout/scout_json.c tests/test_scout_json.c include/scout_json.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/scout/scout_json.c tests/test_scout_json.c

$(BUILD_DIR)/test_c_parser: src/parsers/c_parser.c src/scout/scout_json.c tests/test_c_parser.c include/c_parser.h include/scout_json.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DC_PARSER_NO_MAIN -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/parsers/c_parser.c src/scout/scout_json.c tests/test_c_parser.c

# Sample icons of every style (icons/<Style>/*.info) and icons/preview.png,
# drawn by tools/mkicons.py with amiga-devkit/tools/amigaicons.py.
icons:
	python3 tools/mkicons.py

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

-include $(DEPS) $(SCOUT_DEPS) $(CLASS_DEPS)
