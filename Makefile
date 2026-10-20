# Lupe: an interpreter on the spinel runtime.
# SPINEL points at a built spinel checkout (make deps && make).
#
#   make        build build/lupe
#   make test   run test/*.rb under CRuby and Lupe and compare, once as is
#               and once under the GC stress and verify modes of spinel
#   make oracle run the spinel test corpus under CRuby, Lupe and AOT

SPINEL ?= $(HOME)/work/spinel
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
else
FFI_LIBS =
endif
CFLAGS = -O2 -g -std=gnu11 -ffp-contract=off -DSP_INT_OVERFLOW_MODE_$(INT_OVERFLOW) $(RT_CFLAGS) \
         -DLUPE_PACKAGES_DIR='"$(SPINEL)/packages"' -DLUPE_FFI=$(FFI) \
         -Isrc -I$(BUILD) -I$(SPINEL)/lib -I$(SPINEL)/lib/regexp -I$(SPINEL)/vendor/prism/include
LIBS = $(RT_ARCHIVE) $(SPINEL)/build/libprism.a -lm -lcrypt $(RT_LIBS) $(FFI_LIBS)

all: $(BUILD)/lupe

# The generator includes the table and the traits of the spinel source, so
# a change there regenerates the wrappers.
SPINEL_TABLES = $(SPINEL)/src/builtin_ops.c $(SPINEL)/src/builtin_ops.h $(SPINEL)/src/types.c \
                $(SPINEL)/src/types.h $(wildcard $(SPINEL)/src/*.inc)

$(BUILD)/bop_gen: tools/bop_gen.c $(SPINEL_TABLES)
	mkdir -p $(BUILD)
	$(CC) -std=gnu11 -include stdarg.h -I$(SPINEL)/src -o $@ $< \
	  $(SPINEL)/src/types.c $(SPINEL)/src/builtin_names.c

$(BUILD)/bop_gen_out.c: $(BUILD)/bop_gen
	$(BUILD)/bop_gen $@ $(BUILD)/manifest.tsv

# off.flags turns off each wrapper that does not compile or link.
$(BUILD)/off.flags: $(BUILD)/bop_gen_out.c tools/bop_report.rb
	SPINEL=$(SPINEL) RT_ARCHIVE=$(RT_ARCHIVE) RT_CFLAGS="$(RT_CFLAGS)" RT_LIBS="$(RT_LIBS)" \
	  ruby tools/bop_report.rb $(BUILD) > $(BUILD)/bop_report.txt

# spinel.rev records the spinel commit that the binary was built from. The
# oracle compares it with the spinel tree it tests.
$(BUILD)/lupe: src/*.c src/*.h $(BUILD)/bop_gen_out.c $(BUILD)/off.flags
	$(CC) $(CFLAGS) $$(cat $(BUILD)/off.flags) -o $@ src/main.c $(LIBS)
	git -C $(SPINEL) rev-parse HEAD > $(BUILD)/spinel.rev
	echo $(INT_OVERFLOW) > $(BUILD)/int_overflow

# A missing GC root passes the plain run and fails the stress run: the
# collector runs at each allocation, and the verify mode aborts on a freed
# object that is still referenced.
TEST_DIRS = test/m2 test/m3

test: $(BUILD)/lupe
	ruby test/run.rb $(BUILD)/lupe $(TEST_DIRS)
	SPINEL_GC_STRESS=2 SPINEL_GC_VERIFY=1 ruby test/run.rb $(BUILD)/lupe $(TEST_DIRS)

# The oracle: the spinel test corpus under CRuby (.expected), Lupe and AOT,
# in promote mode on both sides. In raise mode, spinel's analysis still
# promotes an Integer where it sees growth (10 ** 30), and a walker with no
# analysis cannot make the same choice.
oracle:
	$(MAKE) BUILD=$(BUILD)/promote INT_OVERFLOW=PROMOTE $(BUILD)/promote/lupe
	ruby tools/oracle.rb --spinel $(SPINEL) --lupe $(BUILD)/promote/lupe \
	  --int-overflow promote --out $(BUILD)/oracle

clean:
	rm -rf $(BUILD)

.PHONY: all test oracle clean
