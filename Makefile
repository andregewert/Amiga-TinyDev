TARGET_CC ?= /opt/amiga/bin/m68k-amigaos-gcc
HOST_CC ?= cc
# build/: everything produced on the way (objects, dependency files, .debug
# files with symbols, host test binaries). bin/: only the finished Amiga
# programs and classes, laid out as they are used on the Amiga.
BUILD_DIR := build
BIN_DIR := bin
TARGET := $(BIN_DIR)/TinyDev
SOURCES := src/main.c src/ui.c src/document.c src/fileio.c src/tree.c src/syntax.c src/minimap.c src/toolbar.c src/settings.c src/splash.c src/prefswin.c src/closeimage.c src/search.c src/find.c src/filescan.c src/locale.c src/bookmarks.c src/bmstore.c src/projconf.c src/projdata.c src/projwin.c src/build.c src/outwin.c src/json.c src/toolwin.c src/bmlist.c src/help.c src/tempfile.c src/scoutlink.c src/symlist.c src/diag.c src/diaglist.c src/navigate.c src/rules.c src/tokenmap.c src/complete.c
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)
CPPFLAGS := -Iinclude
CFLAGS := -std=gnu99 -m68000 -msoft-float -noixemul -Os -Wall -Wextra -Wshadow -Wconversion -Wno-pointer-sign -MMD -MP
LDFLAGS := -m68000 -msoft-float -noixemul

SCOUT_TARGET := $(BIN_DIR)/TinyDev-Scout
SCOUT_SOURCES := src/scout/scout_main.c src/scout/scout_broker.c src/scout/scout_rexx.c src/scout/scout_jobs.c src/scout/scout_walk.c src/scout/scout_watch.c src/scout/scout_window.c src/scout/scout_store.c src/scout/scout_rpc.c src/json.c
SCOUT_OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SCOUT_SOURCES))
SCOUT_DEPS := $(SCOUT_OBJECTS:.o=.d)

PARSER_C_TARGET := $(BIN_DIR)/parsers/c_parser

# HelpViewer (src/helpviewer/): shows a directory of HTML help pages with
# html.gadget (submodule external/html_gadget, for its headers) and
# dirtree.gadget. Its catalogs are src/helpviewer/catalogs/.
HV_TARGET := $(BIN_DIR)/HelpViewer
HV_SOURCES := src/helpviewer/helpviewer.c src/helpviewer/printwin.c src/helpviewer/htmltitle.c
HV_OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(HV_SOURCES))
HV_DEPS := $(HV_OBJECTS:.o=.d)
HTML_GADGET := external/html_gadget

# Help pages (package/help/<language>/, HTML in ISO-8859-1, language drawers
# named like the Amiga locales): copied to bin/Help/, where TinyDev's
# Help command and the HelpViewer find them (PROGDIR:Help/).
HELP_SOURCES := $(shell find package/help -type f 2>/dev/null)
HELP_FILES := $(patsubst package/help/%,$(BIN_DIR)/Help/%,$(HELP_SOURCES))

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

# Henry Spencer's regex library (submodule, POSIX regular expressions for
# the search). Its regex.h and the private *.ih headers are generated with
# its mkh script into build/regex; it is compiled without our warnings.
REGEX_DIR := external/regex
ifeq ($(wildcard $(REGEX_DIR)/regcomp.c),)
$(error $(REGEX_DIR) is missing: git submodule update --init)
endif
REGEX_GEN := $(BUILD_DIR)/regex
REGEX_NAMES := regcomp regexec regerror regfree
REGEX_HEADERS := $(REGEX_GEN)/regex.h $(REGEX_GEN)/regcomp.ih $(REGEX_GEN)/engine.ih $(REGEX_GEN)/regerror.ih
REGEX_CFLAGS := -DPOSIX_MISTAKE -I$(REGEX_GEN) -I$(REGEX_DIR) -w
REGEX_OBJECTS := $(foreach n,$(REGEX_NAMES),$(REGEX_GEN)/$(n).o)
REGEX_HOST_OBJECTS := $(foreach n,$(REGEX_NAMES),$(REGEX_GEN)/host/$(n).o)
LATIN1 := $(wildcard src/*.c src/*/*.c include/*.h include/*/*.h src/classes/*/*.c src/classes/*/*.h src/helpviewer/*.h src/helpviewer/catalogs/* src/scout/*.h src/scout/catalogs/* sfd/*.sfd tests/*.c scripts/* tests-host/* catalogs/* package/*.readme $(HELP_SOURCES) examples/*/*.c examples/*/Makefile examples/*/*.txt examples/*/.tinydev/*.json tests/data/*/* LICENSE Makefile)

# Translations: catalogs/TinyDev.cd holds the built-in English strings,
# catalogs/<language>.ct the translations. include/tinydev_strings.h is in
# git, so building needs Python only when TinyDev.cd changes; the catalogs
# go to bin/Catalogs/<language>/TinyDev.catalog (PROGDIR:Catalogs/).
CATCOMP := $(DEVKIT)/tools/catcomp.py
LANGUAGES := $(basename $(notdir $(wildcard catalogs/*.ct)))
CATALOGS := $(foreach l,$(LANGUAGES),$(BIN_DIR)/Catalogs/$(l)/TinyDev.catalog)
HV_LANGUAGES := $(basename $(notdir $(wildcard src/helpviewer/catalogs/*.ct)))
HV_CATALOGS := $(foreach l,$(HV_LANGUAGES),$(BIN_DIR)/Catalogs/$(l)/HelpViewer.catalog)
SCOUT_LANGUAGES := $(basename $(notdir $(wildcard src/scout/catalogs/*.ct)))
SCOUT_CATALOGS := $(foreach l,$(SCOUT_LANGUAGES),$(BIN_DIR)/Catalogs/$(l)/TinyDev-Scout.catalog)

.PHONY: all clean test icons dist
all: charcheck $(TARGET) $(CLASS_GADGETS) $(SCOUT_TARGET) $(SCOUT_CATALOGS) $(PARSER_C_TARGET) $(CATALOGS) \
     $(HV_TARGET) $(HV_CATALOGS) $(HELP_FILES)

# Included after "all" so that charcheck does not become the default goal.
include $(DEVKIT)/make/charcheck.mk

$(foreach c,$(CLASS_NAMES),$(eval $(call CLASS_RULES,$(c))))

# Documentation (HTML in build/docs, Doxygen on the host; not part of
# "all"): "docs" covers all of TinyDev's own sources, "docs-<class>" the
# public header of a gadget class. Every target fails on a missing or
# wrong comment (amiga-devkit/docs/Dokumentation.md). Generated headers
# (catalog strings, sfdc output) are left out.
include $(DEVKIT)/make/docs.mk
DOC_SOURCES := $(filter-out include/tinydev_strings.h src/helpviewer/helpviewer_strings.h src/scout/scout_strings.h, \
    $(wildcard src/*.c src/scout/*.c src/parsers/*.c src/classes/*/*.c src/classes/*/*.h \
               src/helpviewer/*.c src/helpviewer/*.h \
               include/*.h include/gadgets/*.h tests/*.c))
$(eval $(call DOXYGEN_DOCS,docs,TinyDev,$(firstword $(subst ", ,$(shell sed -n 's/^.define APP_VERSION "\([^ ]*\).*/\1/p' include/editor.h))),$(DOC_SOURCES),$(BUILD_DIR)/docs))
$(foreach c,$(CLASS_NAMES),$(eval $(call DOXYGEN_DOCS,docs-$(c),$(c).gadget,1.0,include/gadgets/$(c).h,$(BUILD_DIR)/docs/$(c))))

ifeq ($(wildcard $(HTML_GADGET)/include/gadgets/html.h),)
$(error $(HTML_GADGET) is missing: git submodule update --init)
endif

src/scout/scout_strings.h: src/scout/catalogs/TinyDev-Scout.cd $(CATCOMP)
	python3 $(CATCOMP) header $< $@ SCOUT_STRINGS

$(BIN_DIR)/Catalogs/%/TinyDev-Scout.catalog: src/scout/catalogs/%.ct src/scout/catalogs/TinyDev-Scout.cd $(CATCOMP)
	@mkdir -p $(dir $@)
	python3 $(CATCOMP) catalog src/scout/catalogs/TinyDev-Scout.cd $< $@

src/helpviewer/helpviewer_strings.h: src/helpviewer/catalogs/HelpViewer.cd $(CATCOMP)
	python3 $(CATCOMP) header $< $@ HELPVIEWER_STRINGS

$(BIN_DIR)/Catalogs/%/HelpViewer.catalog: src/helpviewer/catalogs/%.ct src/helpviewer/catalogs/HelpViewer.cd $(CATCOMP)
	@mkdir -p $(dir $@)
	python3 $(CATCOMP) catalog src/helpviewer/catalogs/HelpViewer.cd $< $@

$(HV_OBJECTS): CPPFLAGS += -Isrc/helpviewer -I$(HTML_GADGET)/include

$(HV_TARGET): $(HV_OBJECTS)
	@mkdir -p $(BIN_DIR)
	$(TARGET_CC) $(LDFLAGS) -Wl,-u,___stkinit -o $@ $(HV_OBJECTS)

$(BIN_DIR)/Help/%: package/help/%
	@mkdir -p $(dir $@)
	cp $< $@

include/tinydev_strings.h: catalogs/TinyDev.cd $(CATCOMP)
	python3 $(CATCOMP) header $< $@ TINYDEV_STRINGS

$(BIN_DIR)/Catalogs/%/TinyDev.catalog: catalogs/%.ct catalogs/TinyDev.cd $(CATCOMP)
	@mkdir -p $(dir $@)
	python3 $(CATCOMP) catalog catalogs/TinyDev.cd $< $@

$(TARGET): $(OBJECTS) $(REGEX_OBJECTS)
	@mkdir -p $(BIN_DIR)
	$(TARGET_CC) $(LDFLAGS) -Wl,-u,___stkinit -o $@ $(OBJECTS) $(REGEX_OBJECTS)

$(REGEX_GEN)/regex.h: $(REGEX_DIR)/regex2.h $(foreach n,$(REGEX_NAMES),$(REGEX_DIR)/$(n).c) $(REGEX_DIR)/mkh
	@mkdir -p $(dir $@)
	sh $(REGEX_DIR)/mkh -i _REGEX_H_ $(REGEX_DIR)/regex2.h $(foreach n,$(REGEX_NAMES),$(REGEX_DIR)/$(n).c) >$@

$(REGEX_GEN)/%.ih: $(REGEX_DIR)/%.c $(REGEX_DIR)/mkh
	@mkdir -p $(dir $@)
	sh $(REGEX_DIR)/mkh -p $< >$@

$(REGEX_GEN)/%.o: $(REGEX_DIR)/%.c $(REGEX_HEADERS)
	$(TARGET_CC) -std=gnu99 -m68000 -msoft-float -noixemul -Os $(REGEX_CFLAGS) -c -o $@ $<

$(REGEX_GEN)/host/%.o: $(REGEX_DIR)/%.c $(REGEX_HEADERS)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu99 -O2 $(REGEX_CFLAGS) -c -o $@ $<

# keep the generated headers (make would delete them as intermediate files)
.SECONDARY: $(REGEX_HEADERS)

# search.c includes the generated regex.h
$(BUILD_DIR)/search.o: CPPFLAGS += -I$(REGEX_GEN)
$(BUILD_DIR)/search.o: | $(REGEX_GEN)/regex.h

$(SCOUT_TARGET): $(SCOUT_OBJECTS)
	@mkdir -p $(BIN_DIR)
	$(TARGET_CC) $(LDFLAGS) -Wl,-u,___stkinit -o $@ $(SCOUT_OBJECTS) -lamiga

# Compiled and linked in one step, so without -MMD (the .d file would land
# in bin/); the prerequisites name the headers.
$(PARSER_C_TARGET): src/parsers/c_parser.c src/parsers/c_parser_rpc.c src/scout/scout_rpc.c src/json.c include/c_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h
	@mkdir -p $(BIN_DIR)/parsers
	$(TARGET_CC) $(CPPFLAGS) $(filter-out -MMD -MP,$(CFLAGS)) $(LDFLAGS) -Wl,-u,___stkinit -o $@ src/parsers/c_parser.c src/parsers/c_parser_rpc.c src/scout/scout_rpc.c src/json.c

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(TARGET_CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

test: $(BUILD_DIR)/test_syntax $(BUILD_DIR)/test_search $(BUILD_DIR)/test_bmstore $(BUILD_DIR)/test_projconf $(BUILD_DIR)/test_dirtree_sort $(BUILD_DIR)/test_htmltitle $(BUILD_DIR)/test_json $(BUILD_DIR)/test_scout_rpc $(BUILD_DIR)/test_c_parser $(BUILD_DIR)/test_scout_store $(BUILD_DIR)/test_rules
	./$(BUILD_DIR)/test_syntax
	./$(BUILD_DIR)/test_search
	./$(BUILD_DIR)/test_bmstore
	./$(BUILD_DIR)/test_projconf
	./$(BUILD_DIR)/test_dirtree_sort
	./$(BUILD_DIR)/test_htmltitle
	./$(BUILD_DIR)/test_json
	./$(BUILD_DIR)/test_scout_rpc
	./$(BUILD_DIR)/test_c_parser
	./$(BUILD_DIR)/test_scout_store
	./$(BUILD_DIR)/test_rules

$(BUILD_DIR)/test_syntax: src/syntax.c src/rules.c src/tokenmap.c src/json.c tests/test_syntax.c include/syntax.h include/rules.h include/tokenmap.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DSYNTAX_HOST_TEST -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/syntax.c src/rules.c src/tokenmap.c src/json.c tests/test_syntax.c

$(BUILD_DIR)/test_search: src/search.c tests/test_search.c include/search.h $(REGEX_HOST_OBJECTS)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -I$(REGEX_GEN) -std=gnu99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/search.c tests/test_search.c $(REGEX_HOST_OBJECTS)

$(BUILD_DIR)/test_bmstore: src/bmstore.c src/json.c tests/test_bmstore.c include/bmstore.h include/json.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/bmstore.c src/json.c tests/test_bmstore.c

$(BUILD_DIR)/test_htmltitle: src/helpviewer/htmltitle.c tests/test_htmltitle.c src/helpviewer/htmltitle.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Isrc/helpviewer -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/helpviewer/htmltitle.c tests/test_htmltitle.c

$(BUILD_DIR)/test_projconf: src/projconf.c src/json.c tests/test_projconf.c include/projconf.h include/json.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/projconf.c src/json.c tests/test_projconf.c

$(BUILD_DIR)/test_dirtree_sort: src/classes/dirtree/dirtree_sort.c tests/test_dirtree_sort.c src/classes/dirtree/dirtree_sort.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Isrc/classes/dirtree -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/classes/dirtree/dirtree_sort.c tests/test_dirtree_sort.c

$(BUILD_DIR)/test_json: src/json.c tests/test_json.c include/json.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/json.c tests/test_json.c

$(BUILD_DIR)/test_scout_rpc: src/scout/scout_rpc.c src/json.c tests/test_scout_rpc.c include/scout_rpc.h include/json.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/scout/scout_rpc.c src/json.c tests/test_scout_rpc.c

$(BUILD_DIR)/test_c_parser: src/parsers/c_parser.c src/parsers/c_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_c_parser.c include/c_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h $(wildcard tests/data/c_parser/*)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DC_PARSER_NO_MAIN -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/parsers/c_parser.c src/parsers/c_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_c_parser.c

$(BUILD_DIR)/test_scout_store: src/scout/scout_store.c src/parsers/c_parser.c src/parsers/c_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_scout_store.c include/scout_store.h include/c_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h $(wildcard tests/data/c_parser/*)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DC_PARSER_NO_MAIN -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/scout/scout_store.c src/parsers/c_parser.c src/parsers/c_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_scout_store.c

$(BUILD_DIR)/test_rules: src/rules.c src/tokenmap.c src/syntax.c src/json.c src/parsers/c_parser.c src/parsers/c_parser_rpc.c src/scout/scout_rpc.c tests/test_rules.c include/rules.h include/tokenmap.h include/syntax.h include/c_parser.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DSYNTAX_HOST_TEST -DC_PARSER_NO_MAIN -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/rules.c src/tokenmap.c src/syntax.c src/json.c src/parsers/c_parser.c src/parsers/c_parser_rpc.c src/scout/scout_rpc.c tests/test_rules.c

# Sample icons of every style (icons/<Style>/*.info) and icons/preview.png,
# drawn by tools/mkicons.py with amiga-devkit/tools/amigaicons.py.
icons:
	python3 tools/mkicons.py

# Aminet archive dist/TinyDev.lha (+ TinyDev.readme), see tools/mkdist.py
dist: all
	python3 tools/mkdist.py

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) dist

-include $(DEPS) $(SCOUT_DEPS) $(CLASS_DEPS) $(HV_DEPS)
