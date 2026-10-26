# Fluxpinel: an interpreter on the spinel runtime.
#
#   make deps   fetch spinel at the commit in SPINEL_COMMIT into deps/spinel
#               and build it
#   make        build build/fluxpinel
#   make test   run test/*.rb under CRuby and Fluxpinel and compare, once as is
#               and once under the GC stress and verify modes of spinel
#   make oracle run the spinel test corpus under CRuby, Fluxpinel and AOT
#   make regress
#               run the tests that the last oracle found right, under
#               Fluxpinel only
#   make optcarrot
#               run optcarrot under CRuby and Fluxpinel and compare the
#               checksums (OPTCARROT_FRAMES=n for a short run)
#   make upgrade_spinel
#               move the pin to the master of spinel, then build and run
#               test, test-host and regress
#   make roundhouse, make test-roundhouse, make upgrade_roundhouse
#               make the blog of Roundhouse and check it, and move its pin
#               (roundhouse/roundhouse.mk)
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
ifeq ($(filter deps clean upgrade_spinel,$(MAKECMDGOALS)),)
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
# The runtime of spinel holds a String as its handle (SP_ONE_STRING).
CFLAGS = -O2 -g -std=gnu11 -ffp-contract=off -DSP_ONE_STRING -DSP_INT_OVERFLOW_MODE_$(INT_OVERFLOW) $(RT_CFLAGS) \
         -DFX_PACKAGES_DIR='"$(SPINEL)/packages"' -DFX_FFI=$(FFI) \
         -Isrc -I$(BUILD) -I$(SPINEL)/lib -I$(SPINEL)/lib/regexp -I$(SPINEL)/vendor/prism/include
LIBS = $(RT_ARCHIVE) $(SPINEL)/build/libprism.a -lm -lcrypt $(RT_LIBS) $(FFI_LIBS)

all: $(BUILD)/fluxpinel

# The generator includes the table and the traits of the spinel source, so
# a change there regenerates the wrappers.
SPINEL_TABLES = $(SPINEL)/src/builtin_ops.c $(SPINEL)/src/builtin_ops.h $(SPINEL)/src/types.c \
                $(SPINEL)/src/types.h $(wildcard $(SPINEL)/src/*.inc)

$(BUILD)/bop_gen: tools/bop_gen.c tools/bop_gen_decls.h $(SPINEL_TABLES) SPINEL_COMMIT
	mkdir -p $(BUILD)
	$(CC) -std=gnu11 -include stdarg.h -include builtin_names.h -include tools/bop_gen_decls.h \
	  -I$(SPINEL)/src -o $@ $< \
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

# the builtin methods with a block that src/prelude.rb writes in Ruby
$(BUILD)/prelude.inc: src/prelude.rb tools/prelude_gen.rb
	mkdir -p $(BUILD)
	ruby tools/prelude_gen.rb src/prelude.rb > $@

$(BUILD)/fluxpinel: src/*.c src/*.h $(BUILD)/bop_gen_out.c $(BUILD)/off.flags $(BUILD)/prelude.inc $(NATIVE_FLAGS)
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
	ruby test/console.rb $(BUILD)/fluxpinel test/console
	SPINEL_GC_STRESS=2 SPINEL_GC_VERIFY=1 ruby test/console.rb $(BUILD)/fluxpinel test/console

# The host build (docs/design.md, section 3.6): a kernel, a program that
# spinel emits as a library, and Fluxpinel in one TU. The runtime keeps its
# exception stack and other state static in each TU, so the walker and the
# kernel must share the TU to share that state. KERNEL names the program
# and KERNEL_ENTRIES the methods that it exports.
KERNEL ?= test/kernel/app.rb
KERNEL_ENTRIES ?= FxKernel.ping,FxKernel.add,FxKernel.make,FxKernel.bump,FxKernel.boom,FxKernel.pair,$\
                  FxKernel.greet_all,FxKernel.script,FxKernel.rescue_script,FxKernel.console,$\
                  FxKernel.shape,FxKernel.grow,FxKernel.describe
HOST = $(BUILD)/host
# The binary is $(HOST)/$(HOST_NAME). With KERNEL_MAIN (an entry such as
# App.main), it takes no script and calls that entry with ARGV.
HOST_NAME = fluxpinel
KERNEL_MAIN =
KERNEL_OVERFLOW = $(shell echo $(INT_OVERFLOW) | tr A-Z a-z)
# KERNEL_FLAGS: more flags of spinel for the kernel, such as the ones that
# `spin flags` prints for a spin project (-I DIR --rbs DIR). HOST_LIBS: more
# libraries for the link, such as the ones that the kernel binds with
# ffi_func (-lsqlite3).
KERNEL_FLAGS =
HOST_LIBS =

# The variables of each step go in a file that changes only when a value
# changes, so a new value makes the step again: kernel.args for the
# kernel C, link.args for the link.
KERNEL_ARGS = $(KERNEL) $(KERNEL_ENTRIES) $(KERNEL_FLAGS) $(KERNEL_OVERFLOW)
LINK_ARGS = $(KERNEL_MAIN) $(HOST_LIBS)

$(HOST)/kernel.args: FORCE
	@mkdir -p $(HOST)
	@echo '$(KERNEL_ARGS)' | cmp -s - $@ || echo '$(KERNEL_ARGS)' > $@

$(HOST)/link.args: FORCE
	@mkdir -p $(HOST)
	@echo '$(LINK_ARGS)' | cmp -s - $@ || echo '$(LINK_ARGS)' > $@

# kernel.d names each source file of the kernel, so a change of a file that
# KERNEL requires makes the kernel C again. The #line directives of the
# kernel C name the files. A name with no file (a source of a Roundhouse
# view, for example) does not go in. Each file is also a target with no
# rule, so a removed file does not stop make.
-include $(HOST)/kernel.d

$(HOST)/kernel.c: $(KERNEL) kernel/fluxpinel.rb src/kernel_prelude.h SPINEL_COMMIT $(HOST)/kernel.args
	mkdir -p $(HOST)
	$(SPINEL)/spinel $(KERNEL) -c -o $@ --force --int-overflow $(KERNEL_OVERFLOW) $(KERNEL_FLAGS) \
	  --ext-init fx_kernel_init --ext-entry $(KERNEL_ENTRIES)
	# the defines of the kernel before spinel_rt.h, for kernel_prelude.h
	sed '/^#include "spinel_rt.h"/q' $@ | grep '^#define' > $(HOST)/kernel_defines.h || { rm -f $@; exit 1; }
	{ grep -o '^#line [0-9]* "[^"<]*"' $@ || true; } | cut -d'"' -f2 | sort -u | \
	  while read f; do if test -f "$$f"; then echo "$$f"; fi; done > $(HOST)/kernel.srcs
	{ printf '$@:'; tr '\n' ' ' < $(HOST)/kernel.srcs; echo; sed 's/$$/:/' $(HOST)/kernel.srcs; } > $(HOST)/kernel.d

# the wrapper of each entry (host.c)
$(HOST)/entries.c: $(HOST)/kernel.c tools/entry_gen.rb tools/c_box.rb
	ruby tools/entry_gen.rb $(KERNEL_ENTRIES) $(HOST)/kernel.h > $@

# the box of each kind of ivar of the kernel (src/object.c)
$(HOST)/ivars.c: $(HOST)/kernel.c tools/ivar_gen.rb tools/c_box.rb
	ruby tools/ivar_gen.rb $(HOST)/kernel.c > $@

$(HOST)/$(HOST_NAME): src/*.c src/*.h $(HOST)/kernel.c $(HOST)/entries.c $(HOST)/ivars.c $(BUILD)/bop_gen_out.c \
              $(BUILD)/off.flags $(BUILD)/prelude.inc $(NATIVE_FLAGS) $(HOST)/link.args
	$(CC) $(CFLAGS) -I$(HOST) -DFX_KERNEL=fx_kernel_init $(if $(KERNEL_MAIN),'-DFX_KERNEL_MAIN="$(KERNEL_MAIN)"') \
	  -include src/kernel_prelude.h -include $(HOST)/kernel.c \
	  -include $(HOST)/entries.c -include $(HOST)/ivars.c $$(cat $(BUILD)/off.flags) -o $@ src/main.c \
	  $(if $(NATIVE_FLAGS),$$(cat $(NATIVE_FLAGS))) $(LIBS) $(HOST_LIBS)

host: $(HOST)/$(HOST_NAME)

HOST_REQUIRES = --require kernel/fluxpinel_cruby.rb --require $(KERNEL)

test-host: $(HOST)/fluxpinel
	ruby test/run.rb $(HOST_REQUIRES) $(HOST)/fluxpinel $(TEST_DIRS) test/host
	SPINEL_GC_STRESS=2 SPINEL_GC_VERIFY=1 ruby test/run.rb $(HOST_REQUIRES) $(HOST)/fluxpinel $(TEST_DIRS) test/host

include roundhouse/roundhouse.mk

# The oracle: the spinel test corpus under CRuby (.expected), Fluxpinel and AOT,
# in promote mode on both sides. In raise mode, spinel's analysis still
# promotes an Integer where it sees growth (10 ** 30), and a walker with no
# analysis cannot make the same choice.
oracle:
	$(MAKE) BUILD=$(BUILD)/promote INT_OVERFLOW=PROMOTE $(BUILD)/promote/fluxpinel
	ruby tools/oracle.rb --spinel $(SPINEL) --fluxpinel $(BUILD)/promote/fluxpinel \
	  --int-overflow promote --out $(BUILD)/oracle --write-agree tools/oracle_agree.txt

# The tests that the last oracle found right, under Fluxpinel only. It
# finds in seconds a change of spinel that breaks Fluxpinel.
regress:
	$(MAKE) BUILD=$(BUILD)/promote INT_OVERFLOW=PROMOTE $(BUILD)/promote/fluxpinel
	ruby tools/oracle.rb --spinel $(SPINEL) --fluxpinel $(BUILD)/promote/fluxpinel \
	  --int-overflow promote --out $(BUILD)/regress --regress tools/oracle_agree.txt

# optcarrot at the commit in OPTCARROT_COMMIT (the branch that spinel tests),
# under CRuby and Fluxpinel in promote mode. Both sides must give the same
# checksum. OPTCARROT_FRAMES sets the frames.
DEPS_OPTCARROT = $(CURDIR)/deps/optcarrot
OPTCARROT_URL ?= https://github.com/mame/optcarrot.git
OPTCARROT_COMMIT := $(shell cat OPTCARROT_COMMIT)
OPTCARROT_FRAMES ?= 180

optcarrot:
	if [ ! -d $(DEPS_OPTCARROT)/.git ]; then \
	  git init -q $(DEPS_OPTCARROT) && git -C $(DEPS_OPTCARROT) remote add origin $(OPTCARROT_URL); fi
	if [ "$$(git -C $(DEPS_OPTCARROT) rev-parse -q --verify HEAD)" != "$(OPTCARROT_COMMIT)" ]; then \
	  git -C $(DEPS_OPTCARROT) fetch -q --depth 1 origin $(OPTCARROT_COMMIT) && \
	  git -C $(DEPS_OPTCARROT) checkout -q --detach FETCH_HEAD; fi
	$(MAKE) BUILD=$(BUILD)/promote INT_OVERFLOW=PROMOTE $(BUILD)/promote/fluxpinel
	ruby tools/optcarrot.rb --dir $(DEPS_OPTCARROT) --fluxpinel $(BUILD)/promote/fluxpinel \
	  --frames $(OPTCARROT_FRAMES)

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
	$(MAKE) -C $(DEPS_SPINEL)

# Moves the pin to the master of spinel and checks Fluxpinel on it. It stops
# when the checkout has changes. In a checkout of your own, it moves the
# branch master forward and stops on any other branch. It does not commit:
# run make oracle, then commit SPINEL_COMMIT and tools/oracle_agree.txt.
upgrade_spinel:
	@if [ ! -d $(SPINEL)/.git ]; then \
	  echo "upgrade_spinel: $(SPINEL) is not a git checkout" >&2; exit 1; fi
	@if [ -n "$$(git -C $(SPINEL) status --porcelain)" ]; then \
	  echo "upgrade_spinel: $(SPINEL) has changes" >&2; \
	  git -C $(SPINEL) status --short >&2; exit 1; fi
ifeq ($(SPINEL),$(DEPS_SPINEL))
	git -C $(SPINEL) fetch -q --depth 1 $(SPINEL_URL) master
	git -C $(SPINEL) checkout -q --detach FETCH_HEAD
else
	@if [ "$$(git -C $(SPINEL) symbolic-ref -q --short HEAD)" != master ]; then \
	  echo "upgrade_spinel: $(SPINEL) is not on the branch master" >&2; exit 1; fi
	git -C $(SPINEL) fetch -q $(SPINEL_URL) master
	git -C $(SPINEL) merge -q --ff-only FETCH_HEAD
endif
	$(MAKE) -C $(SPINEL) deps
	$(MAKE) -C $(SPINEL)
	git -C $(SPINEL) rev-parse HEAD > SPINEL_COMMIT
	@$(MAKE) all && $(MAKE) test && $(MAKE) test-host && $(MAKE) regress || { \
	  echo "upgrade_spinel: the check failed on $$(cat SPINEL_COMMIT)." \
	    "The pin was $(SPINEL_COMMIT)." >&2; exit 1; }
	@echo "upgrade_spinel: SPINEL_COMMIT is $$(cat SPINEL_COMMIT), from $(SPINEL_COMMIT)."
	@echo "upgrade_spinel: run make oracle before the commit."

clean:
	rm -rf $(BUILD)

.PHONY: all test host test-host oracle regress optcarrot deps upgrade_spinel clean FORCE
