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
CFLAGS = -O2 -g -std=gnu11 -ffp-contract=off -DSP_INT_OVERFLOW_MODE_$(INT_OVERFLOW) \
         -Isrc -I$(BUILD) -I$(SPINEL)/lib -I$(SPINEL)/lib/regexp -I$(SPINEL)/vendor/prism/include
LIBS = $(SPINEL)/lib/libspinel_rt.a $(SPINEL)/build/libprism.a -lm -lcrypt

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
	SPINEL=$(SPINEL) ruby tools/bop_report.rb $(BUILD) > $(BUILD)/bop_report.txt

# spinel.rev records the spinel commit that the binary was built from. The
# oracle compares it with the spinel tree it tests.
$(BUILD)/lupe: src/*.c src/*.h $(BUILD)/bop_gen_out.c $(BUILD)/off.flags
	$(CC) $(CFLAGS) $$(cat $(BUILD)/off.flags) -o $@ src/main.c $(LIBS)
	git -C $(SPINEL) rev-parse HEAD > $(BUILD)/spinel.rev
	echo $(INT_OVERFLOW) > $(BUILD)/int_overflow

# A missing GC root passes the plain run and fails the stress run: the
# collector runs at each allocation, and the verify mode aborts on a freed
# object that is still referenced.
test: $(BUILD)/lupe
	ruby test/run.rb $(BUILD)/lupe test/m2
	SPINEL_GC_STRESS=2 SPINEL_GC_VERIFY=1 ruby test/run.rb $(BUILD)/lupe test/m2

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
