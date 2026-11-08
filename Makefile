TARGET_CC ?= /opt/amiga/bin/m68k-amigaos-gcc
HOST_CC ?= cc
# build/: everything produced on the way (objects, dependency files, .debug
# files with symbols, host test binaries). bin/: only the finished Amiga
# programs and classes, laid out as they are used on the Amiga.
BUILD_DIR := build
BIN_DIR := bin
TARGET := $(BIN_DIR)/TinyDev
SOURCES := src/main.c src/ui.c src/document.c src/fileio.c src/tree.c src/treeops.c src/syntax.c src/minimap.c src/toolbar.c src/settings.c src/splash.c src/prefswin.c src/closeimage.c src/tmplsym.c src/newproj.c src/sprout/sprout_tmpl.c src/search.c src/find.c src/filescan.c src/locale.c src/bookmarks.c src/bmstore.c src/projconf.c src/projdata.c src/projwin.c src/build.c src/outwin.c src/json.c src/toolwin.c src/bmlist.c src/help.c src/shell.c src/filetypes.c src/ftprefs.c src/tempfile.c src/cmdpath.c src/scoutlink.c src/symlist.c src/diag.c src/diaglist.c src/navigate.c src/rules.c src/tokenmap.c src/complete.c src/statuslamp.c
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
# dirtree.gadget's natural order, also used for the list of file types
OBJECTS += $(BUILD_DIR)/natural_sort.o
DEPS := $(OBJECTS:.o=.d)
CPPFLAGS := -Iinclude
CFLAGS := -std=gnu99 -m68000 -msoft-float -noixemul -Os -Wall -Wextra -Wshadow -Wconversion -Wno-pointer-sign -MMD -MP
LDFLAGS := -m68000 -msoft-float -noixemul

SCOUT_TARGET := $(BIN_DIR)/TinyDev-Scout
SCOUT_SOURCES := src/scout/scout_main.c src/scout/scout_broker.c src/scout/scout_rexx.c src/scout/scout_jobs.c src/scout/scout_walk.c src/scout/scout_watch.c src/scout/scout_window.c src/scout/scout_store.c src/scout/scout_rpc.c src/json.c
SCOUT_OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SCOUT_SOURCES))
SCOUT_DEPS := $(SCOUT_OBJECTS:.o=.d)

# Sprout (src/sprout/): the Shell command that creates a project from a
# template (src/sprout/docs/Sprout.md).
SPROUT_TARGET := $(BIN_DIR)/Sprout
SPROUT_SOURCES := src/sprout/sprout_main.c src/sprout/sprout_tmpl.c src/sprout/sprout_lha.c src/json.c
SPROUT_OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SPROUT_SOURCES))
SPROUT_DEPS := $(SPROUT_OBJECTS:.o=.d)

# mdtohtml (submodule external/mdtools, the Kickdown project): turns a tree of
# Markdown files into HTML pages for the HelpViewer. Only its converter is
# compiled here, unchanged and without our warnings; md4c is a submodule of
# that submodule.
MDTOOLS_DIR := external/mdtools
MD4C_DIR := $(MDTOOLS_DIR)/md4c/src
ifeq ($(and $(wildcard $(MDTOOLS_DIR)/src/mdtree.c),$(wildcard $(MD4C_DIR)/md4c.c)),)
$(error $(MDTOOLS_DIR) is missing: git submodule update --init --recursive)
endif
MDTOHTML_TARGET := $(BIN_DIR)/mdtohtml
MDTOHTML_NAMES := mdtohtml mdtree mdtree_dos mdconv entity_stub fileio
MDTOHTML_OBJ_DIR := $(BUILD_DIR)/mdtools
MDTOHTML_OBJECTS := $(foreach n,$(MDTOHTML_NAMES) md4c md4c-html,$(MDTOHTML_OBJ_DIR)/$(n).o)
MDTOHTML_CFLAGS := -std=gnu99 -m68000 -msoft-float -noixemul -Os -fno-common -w -DMD4C_USE_ASCII \
    -I$(MDTOOLS_DIR)/src -I$(MD4C_DIR)

# The templates that come with TinyDev (templates/<Id>/): copied whole to
# bin/Templates/ (PROGDIR:Templates/). Their file names may contain
# placeholders ({$NAME}), which make and the Shell would take for
# variables, so they are handled with find and cp, never listed here.

PARSER_C_TARGET := $(BIN_DIR)/parsers/c_parser
PARSER_DOS_TARGET := $(BIN_DIR)/parsers/dos_parser
PARSER_MAKE_TARGET := $(BIN_DIR)/parsers/make_parser
PARSER_MARKDOWN_TARGET := $(BIN_DIR)/parsers/markdown_parser
PARSER_JSON_TARGET := $(BIN_DIR)/parsers/json_parser
PARSER_MARKUP_TARGET := $(BIN_DIR)/parsers/markup_parser
PARSER_AREXX_TARGET := $(BIN_DIR)/parsers/arexx_parser
PARSER_ASM_TARGET := $(BIN_DIR)/parsers/asm_parser
PARSER_E_TARGET := $(BIN_DIR)/parsers/e_parser
PARSER_ACE_TARGET := $(BIN_DIR)/parsers/ace_parser
PARSER_PASCAL_TARGET := $(BIN_DIR)/parsers/pascal_parser

# HelpViewer (src/helpviewer/): shows a directory of HTML help pages with
# html.gadget (submodule external/html_gadget, for its headers) and
# dirtree.gadget. Its catalogs are src/helpviewer/catalogs/.
HV_TARGET := $(BIN_DIR)/HelpViewer
HV_SOURCES := src/helpviewer/helpviewer.c src/helpviewer/printwin.c src/helpviewer/htmltitle.c
HV_OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(HV_SOURCES))
HV_DEPS := $(HV_OBJECTS:.o=.d)
HTML_GADGET := external/html_gadget
# OpenURL (submodule external/openurl): only its headers, the library is optional
OPENURL := external/openurl

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
# (the LhA archives of tests/data/sprout/ are binary)
LATIN1 := $(filter-out tests/data/sprout/%,$(wildcard src/*.c src/*/*.c include/*.h include/*/*.h src/classes/*/*.c src/classes/*/*.h src/helpviewer/*.h src/helpviewer/catalogs/* src/scout/*.h src/scout/catalogs/* sfd/*.sfd tests/*.c scripts/* tests-host/* catalogs/* package/*.readme package/Install $(filter %.html,$(HELP_SOURCES)) tests/data/*/* LICENSE Makefile))

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

.PHONY: all clean test icons dist templates charcheck-templates
all: charcheck charcheck-templates $(TARGET) $(CLASS_GADGETS) $(SCOUT_TARGET) $(SCOUT_CATALOGS) $(PARSER_C_TARGET) $(PARSER_DOS_TARGET) $(PARSER_MAKE_TARGET) $(PARSER_MARKDOWN_TARGET) $(PARSER_JSON_TARGET) $(PARSER_MARKUP_TARGET) $(PARSER_AREXX_TARGET) $(PARSER_ASM_TARGET) $(PARSER_E_TARGET) $(PARSER_ACE_TARGET) $(PARSER_PASCAL_TARGET) $(CATALOGS) \
     $(HV_TARGET) $(HV_CATALOGS) $(HELP_FILES) $(SPROUT_TARGET) $(MDTOHTML_TARGET) templates

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
    $(wildcard src/*.c src/scout/*.c src/sprout/*.c src/parsers/*.c src/classes/*/*.c src/classes/*/*.h \
               src/helpviewer/*.c src/helpviewer/*.h \
               include/*.h include/gadgets/*.h tests/*.c))
$(eval $(call DOXYGEN_DOCS,docs,TinyDev,$(firstword $(subst ", ,$(shell sed -n 's/^.define APP_VERSION "\([^ ]*\).*/\1/p' include/editor.h))),$(DOC_SOURCES),$(BUILD_DIR)/docs))
$(foreach c,$(CLASS_NAMES),$(eval $(call DOXYGEN_DOCS,docs-$(c),$(c).gadget,1.0,include/gadgets/$(c).h,$(BUILD_DIR)/docs/$(c))))

ifeq ($(wildcard $(HTML_GADGET)/include/gadgets/html.h),)
$(error $(HTML_GADGET) is missing: git submodule update --init)
endif
ifeq ($(wildcard $(OPENURL)/developer/C/include/libraries/openurl.h),)
$(error $(OPENURL) is missing: git submodule update --init)
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

$(HV_OBJECTS): CPPFLAGS += -Isrc/helpviewer -I$(HTML_GADGET)/include -I$(OPENURL)/developer/C/include

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

$(SPROUT_TARGET): $(SPROUT_OBJECTS)
	@mkdir -p $(BIN_DIR)
	$(TARGET_CC) $(LDFLAGS) -Wl,-u,___stkinit -o $@ $(SPROUT_OBJECTS)

$(MDTOHTML_OBJ_DIR)/%.o: $(MDTOOLS_DIR)/src/%.c
	@mkdir -p $(dir $@)
	$(TARGET_CC) $(MDTOHTML_CFLAGS) -c $< -o $@

$(MDTOHTML_OBJ_DIR)/%.o: $(MD4C_DIR)/%.c
	@mkdir -p $(dir $@)
	$(TARGET_CC) $(MDTOHTML_CFLAGS) -c $< -o $@

$(MDTOHTML_TARGET): $(MDTOHTML_OBJECTS)
	@mkdir -p $(BIN_DIR)
	$(TARGET_CC) $(LDFLAGS) -Wl,-u,___stkinit -o $(MDTOHTML_OBJ_DIR)/mdtohtml.debug $(MDTOHTML_OBJECTS)
	$(TARGET_STRIP) -o $@ $(MDTOHTML_OBJ_DIR)/mdtohtml.debug

# copied again every time (cheap), so removed files go too
templates:
	@rm -rf $(BIN_DIR)/Templates
	@mkdir -p $(BIN_DIR)/Templates
	@if [ -d templates ]; then cp -a templates/. $(BIN_DIR)/Templates/; fi

# charcheck for the templates' text files (see "templates" above for why
# not through LATIN1); icons are binary
charcheck-templates:
	@if [ -d templates ] && find templates -type f ! -name '*.info' -exec env LC_ALL=C grep -lP '[\xC2-\xF4][\x80-\xBF]' {} + ; then \
		echo "*** The files above contain UTF-8, please convert them to ISO-8859-1"; exit 1; fi

$(SCOUT_TARGET): $(SCOUT_OBJECTS)
	@mkdir -p $(BIN_DIR)
	$(TARGET_CC) $(LDFLAGS) -Wl,-u,___stkinit -o $@ $(SCOUT_OBJECTS) -lamiga

# Compiled and linked in one step, so without -MMD (the .d file would land
# in bin/); the prerequisites name the headers.
$(PARSER_C_TARGET): src/parsers/c_parser.c src/parsers/c_parser_rpc.c src/scout/scout_rpc.c src/json.c include/c_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h
	@mkdir -p $(BIN_DIR)/parsers
	$(TARGET_CC) $(CPPFLAGS) $(filter-out -MMD -MP,$(CFLAGS)) $(LDFLAGS) -Wl,-u,___stkinit -o $@ src/parsers/c_parser.c src/parsers/c_parser_rpc.c src/scout/scout_rpc.c src/json.c

$(PARSER_DOS_TARGET): src/parsers/dos_parser.c src/parsers/dos_parser_rpc.c src/scout/scout_rpc.c src/json.c include/dos_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h
	@mkdir -p $(BIN_DIR)/parsers
	$(TARGET_CC) $(CPPFLAGS) $(filter-out -MMD -MP,$(CFLAGS)) $(LDFLAGS) -Wl,-u,___stkinit -o $@ src/parsers/dos_parser.c src/parsers/dos_parser_rpc.c src/scout/scout_rpc.c src/json.c

$(PARSER_MAKE_TARGET): src/parsers/make_parser.c src/parsers/make_parser_rpc.c src/scout/scout_rpc.c src/json.c include/make_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h
	@mkdir -p $(BIN_DIR)/parsers
	$(TARGET_CC) $(CPPFLAGS) $(filter-out -MMD -MP,$(CFLAGS)) $(LDFLAGS) -Wl,-u,___stkinit -o $@ src/parsers/make_parser.c src/parsers/make_parser_rpc.c src/scout/scout_rpc.c src/json.c

$(PARSER_MARKDOWN_TARGET): src/parsers/markdown_parser.c src/parsers/markdown_parser_rpc.c src/scout/scout_rpc.c src/json.c include/markdown_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h
	@mkdir -p $(BIN_DIR)/parsers
	$(TARGET_CC) $(CPPFLAGS) $(filter-out -MMD -MP,$(CFLAGS)) $(LDFLAGS) -Wl,-u,___stkinit -o $@ src/parsers/markdown_parser.c src/parsers/markdown_parser_rpc.c src/scout/scout_rpc.c src/json.c

$(PARSER_JSON_TARGET): src/parsers/json_parser.c src/parsers/json_parser_rpc.c src/scout/scout_rpc.c src/json.c include/json_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h
	@mkdir -p $(BIN_DIR)/parsers
	$(TARGET_CC) $(CPPFLAGS) $(filter-out -MMD -MP,$(CFLAGS)) $(LDFLAGS) -Wl,-u,___stkinit -o $@ src/parsers/json_parser.c src/parsers/json_parser_rpc.c src/scout/scout_rpc.c src/json.c

$(PARSER_MARKUP_TARGET): src/parsers/markup_parser.c src/parsers/markup_parser_rpc.c src/scout/scout_rpc.c src/json.c include/markup_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h
	@mkdir -p $(BIN_DIR)/parsers
	$(TARGET_CC) $(CPPFLAGS) $(filter-out -MMD -MP,$(CFLAGS)) $(LDFLAGS) -Wl,-u,___stkinit -o $@ src/parsers/markup_parser.c src/parsers/markup_parser_rpc.c src/scout/scout_rpc.c src/json.c

$(PARSER_AREXX_TARGET): src/parsers/arexx_parser.c src/parsers/arexx_parser_rpc.c src/scout/scout_rpc.c src/json.c include/arexx_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h
	@mkdir -p $(BIN_DIR)/parsers
	$(TARGET_CC) $(CPPFLAGS) $(filter-out -MMD -MP,$(CFLAGS)) $(LDFLAGS) -Wl,-u,___stkinit -o $@ src/parsers/arexx_parser.c src/parsers/arexx_parser_rpc.c src/scout/scout_rpc.c src/json.c

$(PARSER_ASM_TARGET): src/parsers/asm_parser.c src/parsers/asm_parser_rpc.c src/scout/scout_rpc.c src/json.c include/asm_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h
	@mkdir -p $(BIN_DIR)/parsers
	$(TARGET_CC) $(CPPFLAGS) $(filter-out -MMD -MP,$(CFLAGS)) $(LDFLAGS) -Wl,-u,___stkinit -o $@ src/parsers/asm_parser.c src/parsers/asm_parser_rpc.c src/scout/scout_rpc.c src/json.c

$(PARSER_E_TARGET): src/parsers/e_parser.c src/parsers/e_parser_rpc.c src/scout/scout_rpc.c src/json.c include/e_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h
	@mkdir -p $(BIN_DIR)/parsers
	$(TARGET_CC) $(CPPFLAGS) $(filter-out -MMD -MP,$(CFLAGS)) $(LDFLAGS) -Wl,-u,___stkinit -o $@ src/parsers/e_parser.c src/parsers/e_parser_rpc.c src/scout/scout_rpc.c src/json.c

$(PARSER_ACE_TARGET): src/parsers/ace_parser.c src/parsers/ace_parser_rpc.c src/scout/scout_rpc.c src/json.c include/ace_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h
	@mkdir -p $(BIN_DIR)/parsers
	$(TARGET_CC) $(CPPFLAGS) $(filter-out -MMD -MP,$(CFLAGS)) $(LDFLAGS) -Wl,-u,___stkinit -o $@ src/parsers/ace_parser.c src/parsers/ace_parser_rpc.c src/scout/scout_rpc.c src/json.c

$(PARSER_PASCAL_TARGET): src/parsers/pascal_parser.c src/parsers/pascal_parser_rpc.c src/scout/scout_rpc.c src/json.c include/pascal_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h
	@mkdir -p $(BIN_DIR)/parsers
	$(TARGET_CC) $(CPPFLAGS) $(filter-out -MMD -MP,$(CFLAGS)) $(LDFLAGS) -Wl,-u,___stkinit -o $@ src/parsers/pascal_parser.c src/parsers/pascal_parser_rpc.c src/scout/scout_rpc.c src/json.c

$(BUILD_DIR)/natural_sort.o: src/classes/dirtree/dirtree_sort.c
	@mkdir -p $(dir $@)
	$(TARGET_CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(TARGET_CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

test: $(BUILD_DIR)/test_syntax $(BUILD_DIR)/test_search $(BUILD_DIR)/test_bmstore $(BUILD_DIR)/test_projconf $(BUILD_DIR)/test_dirtree_sort $(BUILD_DIR)/test_htmltitle $(BUILD_DIR)/test_json $(BUILD_DIR)/test_scout_rpc $(BUILD_DIR)/test_c_parser $(BUILD_DIR)/test_dos_parser $(BUILD_DIR)/test_make_parser $(BUILD_DIR)/test_markdown_parser $(BUILD_DIR)/test_json_parser $(BUILD_DIR)/test_markup_parser $(BUILD_DIR)/test_arexx_parser $(BUILD_DIR)/test_asm_parser $(BUILD_DIR)/test_e_parser $(BUILD_DIR)/test_ace_parser $(BUILD_DIR)/test_pascal_parser $(BUILD_DIR)/test_scout_store $(BUILD_DIR)/test_rules $(BUILD_DIR)/test_filetypes $(BUILD_DIR)/test_sprout_tmpl $(BUILD_DIR)/test_sprout_lha
	./$(BUILD_DIR)/test_syntax
	./$(BUILD_DIR)/test_search
	./$(BUILD_DIR)/test_bmstore
	./$(BUILD_DIR)/test_projconf
	./$(BUILD_DIR)/test_dirtree_sort
	./$(BUILD_DIR)/test_htmltitle
	./$(BUILD_DIR)/test_json
	./$(BUILD_DIR)/test_scout_rpc
	./$(BUILD_DIR)/test_c_parser
	./$(BUILD_DIR)/test_dos_parser
	./$(BUILD_DIR)/test_make_parser
	./$(BUILD_DIR)/test_markdown_parser
	./$(BUILD_DIR)/test_json_parser
	./$(BUILD_DIR)/test_markup_parser
	./$(BUILD_DIR)/test_arexx_parser
	./$(BUILD_DIR)/test_asm_parser
	./$(BUILD_DIR)/test_e_parser
	./$(BUILD_DIR)/test_ace_parser
	./$(BUILD_DIR)/test_pascal_parser
	./$(BUILD_DIR)/test_scout_store
	./$(BUILD_DIR)/test_rules
	./$(BUILD_DIR)/test_filetypes
	./$(BUILD_DIR)/test_sprout_tmpl
	./$(BUILD_DIR)/test_sprout_lha

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

$(BUILD_DIR)/test_sprout_lha: src/sprout/sprout_lha.c tests/test_sprout_lha.c include/sprout_lha.h $(wildcard tests/data/sprout/*)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/sprout/sprout_lha.c tests/test_sprout_lha.c

$(BUILD_DIR)/test_sprout_tmpl: src/sprout/sprout_tmpl.c src/json.c tests/test_sprout_tmpl.c include/sprout_tmpl.h include/json.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/sprout/sprout_tmpl.c src/json.c tests/test_sprout_tmpl.c

$(BUILD_DIR)/test_projconf: src/projconf.c src/json.c tests/test_projconf.c include/projconf.h include/json.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/projconf.c src/json.c tests/test_projconf.c

$(BUILD_DIR)/test_filetypes: src/filetypes.c src/json.c tests/test_filetypes.c include/filetypes.h include/json.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/filetypes.c src/json.c tests/test_filetypes.c

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

$(BUILD_DIR)/test_dos_parser: src/parsers/dos_parser.c src/parsers/dos_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_dos_parser.c include/dos_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h $(wildcard tests/data/dos_parser/*)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DDOS_PARSER_NO_MAIN -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/parsers/dos_parser.c src/parsers/dos_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_dos_parser.c

$(BUILD_DIR)/test_make_parser: src/parsers/make_parser.c src/parsers/make_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_make_parser.c include/make_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DMAKE_PARSER_NO_MAIN -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/parsers/make_parser.c src/parsers/make_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_make_parser.c

$(BUILD_DIR)/test_markdown_parser: src/parsers/markdown_parser.c src/parsers/markdown_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_markdown_parser.c include/markdown_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h $(wildcard tests/data/markdown_parser/*)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DMARKDOWN_PARSER_NO_MAIN -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/parsers/markdown_parser.c src/parsers/markdown_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_markdown_parser.c

$(BUILD_DIR)/test_json_parser: src/parsers/json_parser.c src/parsers/json_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_json_parser.c include/json_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h $(wildcard tests/data/json_parser/*)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DJSON_PARSER_NO_MAIN -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/parsers/json_parser.c src/parsers/json_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_json_parser.c

$(BUILD_DIR)/test_markup_parser: src/parsers/markup_parser.c src/parsers/markup_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_markup_parser.c include/markup_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h $(wildcard tests/data/markup_parser/*)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DMARKUP_PARSER_NO_MAIN -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/parsers/markup_parser.c src/parsers/markup_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_markup_parser.c

$(BUILD_DIR)/test_arexx_parser: src/parsers/arexx_parser.c src/parsers/arexx_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_arexx_parser.c include/arexx_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h $(wildcard tests/data/arexx_parser/*)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DAREXX_PARSER_NO_MAIN -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/parsers/arexx_parser.c src/parsers/arexx_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_arexx_parser.c

$(BUILD_DIR)/test_asm_parser: src/parsers/asm_parser.c src/parsers/asm_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_asm_parser.c include/asm_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h $(wildcard tests/data/asm_parser/*)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DASM_PARSER_NO_MAIN -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/parsers/asm_parser.c src/parsers/asm_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_asm_parser.c

$(BUILD_DIR)/test_e_parser: src/parsers/e_parser.c src/parsers/e_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_e_parser.c include/e_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h $(wildcard tests/data/e_parser/*)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DE_PARSER_NO_MAIN -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/parsers/e_parser.c src/parsers/e_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_e_parser.c

$(BUILD_DIR)/test_ace_parser: src/parsers/ace_parser.c src/parsers/ace_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_ace_parser.c include/ace_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h $(wildcard tests/data/ace_parser/*)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DACE_PARSER_NO_MAIN -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/parsers/ace_parser.c src/parsers/ace_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_ace_parser.c

$(BUILD_DIR)/test_pascal_parser: src/parsers/pascal_parser.c src/parsers/pascal_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_pascal_parser.c include/pascal_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h $(wildcard tests/data/pascal_parser/*)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DPASCAL_PARSER_NO_MAIN -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/parsers/pascal_parser.c src/parsers/pascal_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_pascal_parser.c

$(BUILD_DIR)/test_scout_store: src/scout/scout_store.c src/parsers/c_parser.c src/parsers/c_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_scout_store.c include/scout_store.h include/c_parser.h include/scout_protocol.h include/scout_rpc.h include/json.h $(wildcard tests/data/c_parser/*)
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DC_PARSER_NO_MAIN -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/scout/scout_store.c src/parsers/c_parser.c src/parsers/c_parser_rpc.c src/scout/scout_rpc.c src/json.c tests/test_scout_store.c

$(BUILD_DIR)/test_rules: src/rules.c src/tokenmap.c src/syntax.c src/json.c src/parsers/c_parser.c src/parsers/c_parser_rpc.c src/parsers/dos_parser.c src/parsers/dos_parser_rpc.c src/scout/scout_rpc.c tests/test_rules.c include/rules.h include/tokenmap.h include/syntax.h include/c_parser.h include/dos_parser.h
	@mkdir -p $(BUILD_DIR)
	$(HOST_CC) -Iinclude -DSYNTAX_HOST_TEST -DC_PARSER_NO_MAIN -DDOS_PARSER_NO_MAIN -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -o $@ src/rules.c src/tokenmap.c src/syntax.c src/json.c src/parsers/c_parser.c src/parsers/c_parser_rpc.c src/parsers/dos_parser.c src/parsers/dos_parser_rpc.c src/scout/scout_rpc.c tests/test_rules.c

# Sample icons of every style (icons/<Style>/*.info) and icons/preview.png,
# drawn by tools/mkicons.py with amiga-devkit/tools/amigaicons.py.
icons:
	python3 tools/mkicons.py

# Aminet archive dist/TinyDev.lha (+ TinyDev.readme), see tools/mkdist.py
dist: all
	python3 tools/mkdist.py

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) dist

-include $(DEPS) $(SCOUT_DEPS) $(CLASS_DEPS) $(HV_DEPS) $(SPROUT_DEPS)
