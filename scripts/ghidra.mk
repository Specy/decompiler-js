# Builds Ghidra's decompiler, the SLEIGH compiler and our host layer, for the
# host compiler or for Emscripten. The source lists follow upstream's own
# Makefile (ghidra/decompile/cpp/Makefile) and buildNatives.gradle.
#
# Called by scripts/compile_specs.sh, scripts/compile_native.sh and
# scripts/compile_decompiler.sh, never by hand:
#
#   make -f scripts/ghidra.mk BUILD=<dir> SRC=<dir> [CXX=… CC=… OPT=… EXTRA_CXXFLAGS=…] <target>
#
# SRC is a directory holding cpp/ and zlib/ (ghidra/decompile, or a patched
# copy of it); BUILD receives the objects and the outputs.

BUILD ?= build/native
SRC ?= ghidra/decompile
HOST ?= host
CXX ?= g++
CC ?= gcc
AR ?= ar
OPT ?= -O2
EXTRA_CXXFLAGS ?=
EXTRA_CFLAGS ?=
EXTRA_LDFLAGS ?=

CPP := $(SRC)/cpp
ZLIB := $(SRC)/zlib

# Upstream builds with -std=c++11; our host layer uses C++17.
GHIDRA_CXXFLAGS := -std=c++11 $(OPT) -Wall -Wno-sign-compare -DLOCAL_ZLIB $(EXTRA_CXXFLAGS)
HOST_CXXFLAGS := -std=c++17 $(OPT) -Wall -Wno-sign-compare -DLOCAL_ZLIB -I$(CPP) $(EXTRA_CXXFLAGS)
ZLIB_CFLAGS := $(OPT) -DNO_GZIP $(EXTRA_CFLAGS)

# Upstream's partition of the sources (Makefile: CORE, DECCORE, SLEIGH, SLACOMP).
CORE := xml marshal space float address pcoderaw translate opcodes globalcontext
DECCORE := capability architecture options graph cover block cast typeop database cpool \
	comment stringmanage modelrules fspec action loadimage grammar varnode op type \
	variable varmap jumptable emulate emulateutil flow userop expression multiprecision \
	funcdata funcdata_block funcdata_op funcdata_varnode unionresolve pcodeinject \
	heritage prefersplit rangeutil ruleaction subflow blockaction merge double \
	transform constseq bitfield coreaction condexe override dynamic crc32 prettyprint \
	printlanguage printc printjava memstate opbehavior paramid signature
SLEIGH := sleigh pcodeparse pcodecompile sleighbase slghsymbol \
	slghpatexpress slghpattern semantics context slaformat compression filemanage
SLACOMP := slgh_compile slghparse slghscan
ZLIB_NAMES := adler32 deflate inffast inflate inftrees trees zutil

# The library a host layer links: the decompiler, the SLEIGH translator and
# SleighArchitecture with its p-code injection (upstream's EXTRA, less the
# console and the loaders we do not use).
LIBRARY := $(CORE) $(DECCORE) $(SLEIGH) sleigh_arch inject_sleigh libdecomp
# Upstream's console (decomp_opt) without the libbfd loader and the two
# command modules that need it (codedata, analyzesigs): it loads XML images.
CONSOLE_EXTRA := callgraph ifacedecomp ifaceterm interface loadimage_xml raw_arch \
	rulecompile testfunction unify xml_arch consolemain

HOST_LIBRARY := $(basename $(notdir $(wildcard $(HOST)/src/*.cc)))
HOST_NATIVE := $(basename $(notdir $(wildcard $(HOST)/native/*.cc)))

obj = $(addprefix $(BUILD)/$(1)/,$(addsuffix .o,$(2)))

LIBRARY_OBJS := $(call obj,ghidra,$(LIBRARY)) $(call obj,zlib,$(ZLIB_NAMES))
SLEIGH_OBJS := $(call obj,ghidra,$(CORE) $(SLEIGH) $(SLACOMP)) $(call obj,zlib,$(ZLIB_NAMES))
CONSOLE_OBJS := $(LIBRARY_OBJS) $(call obj,ghidra,$(CONSOLE_EXTRA))
HOST_OBJS := $(call obj,host,$(HOST_LIBRARY))
NATIVE_OBJS := $(call obj,host-native,$(HOST_NATIVE))

.PHONY: sleigh library host console driver
sleigh: $(BUILD)/sleigh
library: $(BUILD)/libdecomp.a
host: $(BUILD)/libhost.a
console: $(BUILD)/decomp_console
driver: $(BUILD)/decompile

$(BUILD)/sleigh: $(SLEIGH_OBJS)
	$(CXX) $(OPT) -o $@ $^ $(EXTRA_LDFLAGS)

$(BUILD)/libdecomp.a: $(LIBRARY_OBJS)
	rm -f $@
	$(AR) qc $@ $^

$(BUILD)/libhost.a: $(HOST_OBJS)
	rm -f $@
	$(AR) qc $@ $^

$(BUILD)/decomp_console: $(CONSOLE_OBJS)
	$(CXX) $(OPT) -o $@ $^ $(EXTRA_LDFLAGS)

$(BUILD)/decompile: $(NATIVE_OBJS) $(BUILD)/libhost.a $(BUILD)/libdecomp.a
	$(CXX) $(OPT) -o $@ $^ $(EXTRA_LDFLAGS)

# The console's own objects are compiled with __TERMINAL__, as upstream's are.
$(call obj,ghidra,$(CONSOLE_EXTRA)): GHIDRA_CXXFLAGS += -D__TERMINAL__

$(BUILD)/ghidra/%.o: $(CPP)/%.cc
	@mkdir -p $(@D)
	$(CXX) $(GHIDRA_CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/zlib/%.o: $(ZLIB)/%.c
	@mkdir -p $(@D)
	$(CC) $(ZLIB_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/host/%.o: $(HOST)/src/%.cc
	@mkdir -p $(@D)
	$(CXX) $(HOST_CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/host-native/%.o: $(HOST)/native/%.cc
	@mkdir -p $(@D)
	$(CXX) $(HOST_CXXFLAGS) -I$(HOST)/src -MMD -MP -c $< -o $@

-include $(wildcard $(BUILD)/*/*.d)
