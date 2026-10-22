# M1: generate builtin wrappers from spinel bop_rows[] and measure them.
# SPINEL points at a built spinel checkout, as in the top Makefile.
#
#   make report   generate, compile and link the wrappers; print coverage
#   make smoke    call a few wrappers and check the answers

-include ../../config.mk
SPINEL ?= $(abspath ../../deps/spinel)
CC ?= cc
BUILD = build
TOOLS = ../../tools
TU_DIR = ../../src
RT_CFLAGS = -std=gnu11 -O1 -DSP_INT_OVERFLOW_MODE_RAISE \
            -I$(TU_DIR) -I$(BUILD) -I$(SPINEL)/lib -I$(SPINEL)/lib/regexp
RT_LIBS = $(SPINEL)/lib/libspinel_rt.a -lm -lcrypt

all: report smoke

$(BUILD)/bop_gen: $(TOOLS)/bop_gen.c
	mkdir -p $(BUILD)
	$(CC) -std=gnu11 -include stdarg.h -I$(SPINEL)/src -o $@ $< \
	  $(SPINEL)/src/types.c $(SPINEL)/src/builtin_names.c

$(BUILD)/bop_gen_out.c: $(BUILD)/bop_gen
	$(BUILD)/bop_gen $@ $(BUILD)/manifest.tsv

$(BUILD)/off.flags: $(BUILD)/bop_gen_out.c $(TOOLS)/bop_report.rb
	SPINEL=$(SPINEL) FX_TU_DIR=$(abspath $(TU_DIR)) ruby $(TOOLS)/bop_report.rb $(BUILD)

report: $(BUILD)/off.flags

$(BUILD)/smoke: smoke.c $(BUILD)/off.flags
	$(CC) $(RT_CFLAGS) $$(cat $(BUILD)/off.flags) -o $@ smoke.c $(RT_LIBS)

smoke: $(BUILD)/smoke
	$(BUILD)/smoke

clean:
	rm -rf $(BUILD)

.PHONY: all report smoke clean
