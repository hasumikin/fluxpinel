# Fluxpinel: an interpreter on the spinel runtime.
#
#   make deps   fetch spinel at the commit in SPINEL_COMMIT into deps/spinel
#               and build it
#   make        build build/fluxpinel
#   make test   run test/*.rb under CRuby and Fluxpinel and compare, once as is
#               and once under the GC stress and verify modes of spinel
#   make oracle run the spinel test corpus under CRuby, Fluxpinel and AOT
#
# SPINEL points at a built spinel checkout, deps/spinel by default. Set it
# on the command line or in config.mk (not tracked) to use another one,
# e.g. `SPINEL = $(HOME)/src/spinel`. The checkout must be at the commit in
# SPINEL_COMMIT; SPINEL_PIN=0 turns the check off, e.g. to run the oracle
# on another spinel commit.

-include config.mk
DEPS_SPINEL = $(CURDIR)/deps/spinel
SPINEL ?= $(DEPS_SPINEL)
SPINEL_URL ?= https://github.com/matz/spinel.git
SPINEL_COMMIT := $(shell cat SPINEL_COMMIT)
SPINEL_PIN ?= 1
ifeq ($(SPINEL_PIN),1)
ifeq ($(filter deps clean,$(MAKECMDGOALS)),)
SPINEL_HEAD := $(shell git -C $(SPINEL) rev-parse HEAD 2>/dev/null)
ifneq ($(SPINEL_HEAD),$(SPINEL_COMMIT))
$(error $(SPINEL) is at $(or $(SPINEL_HEAD),no commit), and SPINEL_COMMIT is $(SPINEL_COMMIT). \
  $(if $(filter $(DEPS_SPINEL),$(SPINEL)),Run `make deps`,Check out SPINEL_COMMIT in $(SPINEL)), \
  or pass SPINEL_PIN=0)
endif
endif
endif
CC ?= cc
BUILD = build
# RAISE, WRAP or PROMOTE: match spinel --int-overflow of the AOT side.
INT_OVERFLOW ?= RAISE
# 1: the threaded runtime, which spinel links for a program that uses
# threads; its scheduler parks a green thread at sleep and at I/O. 0: the
# single-threaded runtime. Run `make clean` after a change.
THREADS ?= 1
ifeq ($(THREADS),1)
RT_ARCHIVE = $(SPINEL)/lib/libspinel_rt_mt.a
RT_CFLAGS = -DSP_THREADS -ftls-model=initial-exec
RT_LIBS = -lpthread
else
RT_ARCHIVE = $(SPINEL)/lib/libspinel_rt.a
RT_CFLAGS =
RT_LIBS =
endif
# 1: the FFI of spinel (ffi_func and its kin) at run time, through libffi.
# 0: no libffi, and an FFI declaration is a gap.
FFI ?= 1
ifeq ($(FFI),1)
# -rdynamic: an ffi_func with no ffi_lib can name a function of the runtime
# (sp_net_shell_capture), which dlsym finds only in the dynamic symbols
FFI_LIBS = -lffi -ldl -rdynamic
# the objects of the packages and the runtime functions that their
# native_func bindings name (tools/native_link.rb)
NATIVE_FLAGS = $(BUILD)/native.flags
else
FFI_LIBS =
NATIVE_FLAGS =
endif
CFLAGS = -O2 -g -std=gnu11 -ffp-contract=off -DSP_INT_OVERFLOW_MODE_$(INT_OVERFLOW) $(RT_CFLAGS) \
         -DFX_PACKAGES_DIR='"$(SPINEL)/packages"' -DFX_FFI=$(FFI) \
         -Isrc -I$(BUILD) -I$(SPINEL)/lib -I$(SPINEL)/lib/regexp -I$(SPINEL)/vendor/prism/include
LIBS = $(RT_ARCHIVE) $(SPINEL)/build/libprism.a -lm -lcrypt $(RT_LIBS) $(FFI_LIBS)

all: $(BUILD)/fluxpinel

# The generator includes the table and the traits of the spinel source, so
# a change there regenerates the wrappers.
SPINEL_TABLES = $(SPINEL)/src/builtin_ops.c $(SPINEL)/src/builtin_ops.h $(SPINEL)/src/types.c \
                $(SPINEL)/src/types.h $(wildcard $(SPINEL)/src/*.inc)

$(BUILD)/bop_gen: tools/bop_gen.c $(SPINEL_TABLES) SPINEL_COMMIT
	mkdir -p $(BUILD)
	$(CC) -std=gnu11 -include stdarg.h -include builtin_names.h -I$(SPINEL)/src -o $@ $< \
	  $(SPINEL)/src/types.c $(SPINEL)/src/builtin_names.c

$(BUILD)/bop_gen_out.c: $(BUILD)/bop_gen
	$(BUILD)/bop_gen $@ $(BUILD)/manifest.tsv

# off.flags turns off each wrapper that does not compile or link.
$(BUILD)/off.flags: $(BUILD)/bop_gen_out.c tools/bop_report.rb
	SPINEL=$(SPINEL) RT_ARCHIVE=$(RT_ARCHIVE) RT_CFLAGS="$(RT_CFLAGS)" RT_LIBS="$(RT_LIBS)" \
	  ruby tools/bop_report.rb $(BUILD) > $(BUILD)/bop_report.txt

# spinel.rev records the spinel commit that the binary was built from. The
# oracle compares it with the spinel tree it tests.
$(BUILD)/native.flags: tools/native_link.rb $(wildcard $(SPINEL)/packages/*/*.rb)
	mkdir -p $(BUILD)
	ruby tools/native_link.rb $(SPINEL) $(THREADS) $(FFI) > $@

$(BUILD)/fluxpinel: src/*.c src/*.h $(BUILD)/bop_gen_out.c $(BUILD)/off.flags $(NATIVE_FLAGS)
	$(CC) $(CFLAGS) $$(cat $(BUILD)/off.flags) -o $@ src/main.c $(if $(NATIVE_FLAGS),$$(cat $(NATIVE_FLAGS))) \
	  $(LIBS)
	git -C $(SPINEL) rev-parse HEAD > $(BUILD)/spinel.rev
	echo $(INT_OVERFLOW) > $(BUILD)/int_overflow

# A missing GC root passes the plain run and fails the stress run: the
# collector runs at each allocation, and the verify mode aborts on a freed
# object that is still referenced.
TEST_DIRS = test/m2 test/m3

test: $(BUILD)/fluxpinel
	ruby test/run.rb $(BUILD)/fluxpinel $(TEST_DIRS)
	SPINEL_GC_STRESS=2 SPINEL_GC_VERIFY=1 ruby test/run.rb $(BUILD)/fluxpinel $(TEST_DIRS)

# The host build (docs/design.md, section 3.6): a kernel, a program that
# spinel emits as a library, and Fluxpinel in one TU. The runtime keeps its
# exception stack and other state static in each TU, so the walker and the
# kernel must share the TU to share that state. KERNEL names the program
# and KERNEL_ENTRIES the methods that it exports.
KERNEL ?= test/kernel/app.rb
KERNEL_ENTRIES ?= FxKernel.ping,FxKernel.add,FxKernel.make,FxKernel.bump,FxKernel.boom,FxKernel.pair,$\
                  FxKernel.greet_all,FxKernel.script,FxKernel.rescue_script
HOST = $(BUILD)/host
# The binary is $(HOST)/$(HOST_NAME). With KERNEL_MAIN (an entry such as
# App.main), it takes no script and calls that entry with ARGV.
HOST_NAME = fluxpinel
KERNEL_MAIN =
KERNEL_OVERFLOW = $(shell echo $(INT_OVERFLOW) | tr A-Z a-z)

$(HOST)/kernel.c: $(KERNEL) kernel/fluxpinel.rb src/kernel_prelude.h SPINEL_COMMIT
	mkdir -p $(HOST)
	$(SPINEL)/spinel $(KERNEL) -c -o $@ --force --int-overflow $(KERNEL_OVERFLOW) \
	  --ext-init fx_kernel_init --ext-entry $(KERNEL_ENTRIES)
	# the defines of the kernel before spinel_rt.h, for kernel_prelude.h
	sed '/^#include "spinel_rt.h"/q' $@ | grep '^#define' > $(HOST)/kernel_defines.h || { rm -f $@; exit 1; }

# the wrapper of each entry (host.c)
$(HOST)/entries.c: $(HOST)/kernel.c tools/entry_gen.rb
	ruby tools/entry_gen.rb $(KERNEL_ENTRIES) $(HOST)/kernel.h > $@

$(HOST)/$(HOST_NAME): src/*.c src/*.h $(HOST)/kernel.c $(HOST)/entries.c $(BUILD)/bop_gen_out.c \
              $(BUILD)/off.flags $(NATIVE_FLAGS)
	$(CC) $(CFLAGS) -I$(HOST) -DFX_KERNEL=fx_kernel_init $(if $(KERNEL_MAIN),'-DFX_KERNEL_MAIN="$(KERNEL_MAIN)"') \
	  -include src/kernel_prelude.h -include $(HOST)/kernel.c \
	  -include $(HOST)/entries.c $$(cat $(BUILD)/off.flags) -o $@ src/main.c \
	  $(if $(NATIVE_FLAGS),$$(cat $(NATIVE_FLAGS))) $(LIBS)

host: $(HOST)/$(HOST_NAME)

HOST_REQUIRES = --require kernel/fluxpinel_cruby.rb --require $(KERNEL)

test-host: $(HOST)/fluxpinel
	ruby test/run.rb $(HOST_REQUIRES) $(HOST)/fluxpinel $(TEST_DIRS) test/host
	SPINEL_GC_STRESS=2 SPINEL_GC_VERIFY=1 ruby test/run.rb $(HOST_REQUIRES) $(HOST)/fluxpinel $(TEST_DIRS) test/host

# The oracle: the spinel test corpus under CRuby (.expected), Fluxpinel and AOT,
# in promote mode on both sides. In raise mode, spinel's analysis still
# promotes an Integer where it sees growth (10 ** 30), and a walker with no
# analysis cannot make the same choice.
oracle:
	$(MAKE) BUILD=$(BUILD)/promote INT_OVERFLOW=PROMOTE $(BUILD)/promote/fluxpinel
	ruby tools/oracle.rb --spinel $(SPINEL) --fluxpinel $(BUILD)/promote/fluxpinel \
	  --int-overflow promote --out $(BUILD)/oracle

# Fetches only the pinned commit, and builds spinel in place. A checkout
# that SPINEL names elsewhere is yours: make deps does not touch it.
deps:
ifneq ($(SPINEL),$(DEPS_SPINEL))
	$(error make deps fetches into deps/spinel, and SPINEL is $(SPINEL))
endif
	if [ ! -d $(DEPS_SPINEL)/.git ]; then \
	  git init -q $(DEPS_SPINEL) && git -C $(DEPS_SPINEL) remote add origin $(SPINEL_URL); fi
	if [ "$$(git -C $(DEPS_SPINEL) rev-parse -q --verify HEAD)" != "$(SPINEL_COMMIT)" ]; then \
	  git -C $(DEPS_SPINEL) fetch -q --depth 1 origin $(SPINEL_COMMIT) && \
	  git -C $(DEPS_SPINEL) checkout -q --detach FETCH_HEAD; fi
	$(MAKE) -C $(DEPS_SPINEL) deps
	# bin/spinel-cext-build links the threaded objects of these two
	# packages, which spinel's `all` builds after it in a fresh tree
	$(MAKE) -C $(DEPS_SPINEL) packages/json/sp_json_mt.o packages/tmpdir/sp_tmpdir_mt.o
	$(MAKE) -C $(DEPS_SPINEL)

clean:
	rm -rf $(BUILD)

.PHONY: all test host test-host oracle deps clean
