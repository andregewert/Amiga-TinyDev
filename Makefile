TARGET_CC ?= /opt/amiga/bin/m68k-amigaos-gcc
HOST_CC ?= cc
BUILD_DIR := build
TARGET := $(BUILD_DIR)/TinyDev
SOURCES := src/main.c src/ui.c src/document.c src/fileio.c src/tree.c src/syntax.c src/minimap.c src/toolbar.c src/settings.c src/splash.c
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

# Gadget classes (src/classes/<name>/), each a disk-based class library for
# SYS:Classes/Gadgets/. They are written to build/Classes/Gadgets/ so that
# TinyDev finds them as PROGDIR:Classes/Gadgets/<name>.gadget when they are
# not installed. src/classes/common/classlib.c (the library frame) is linked
# first and compiled per class with its class_config.h.
AMIGA_PREFIX := $(patsubst %/bin/m68k-amigaos-gcc,%,$(TARGET_CC))
TARGET_STRIP ?= $(AMIGA_PREFIX)/bin/m68k-amigaos-strip
CLASS_DIR := $(BUILD_DIR)/Classes/Gadgets
# Objects go to build/classobj/<name>/: a build/classes directory would clash
# with build/Classes on the case-insensitive Amiga file system.
CLASS_OBJ_DIR := $(BUILD_DIR)/classobj
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
LATIN1 := $(wildcard src/*.c src/*/*.c include/*.h include/*/*.h src/classes/*/*.c src/classes/*/*.h sfd/*.sfd tests/*.c scripts/* tests-host/* LICENSE Makefile Makefile-Amiga-vbcc)

.PHONY: all clean test
all: charcheck $(TARGET) $(CLASS_GADGETS) $(SCOUT_TARGET) $(PARSER_C_TARGET)

# Included after "all" so that charcheck does not become the default goal.
include $(DEVKIT)/make/charcheck.mk

$(foreach c,$(CLASS_NAMES),$(eval $(call CLASS_RULES,$(c))))

$(TARGET): $(OBJECTS)
	$(TARGET_CC) $(LDFLAGS) -Wl,-u,___stkinit -o $@ $(OBJECTS)

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

-include $(DEPS) $(SCOUT_DEPS) $(CLASS_DEPS)
