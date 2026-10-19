# M0: link libspinel_rt with no spinel-generated code.
# SPINEL points at a built spinel checkout (make deps && make).

SPINEL ?= $(HOME)/work/spinel
CC ?= cc
# RAISE, WRAP or PROMOTE: match spinel --int-overflow of the AOT side.
INT_OVERFLOW ?= RAISE
SRC = ../../src
CFLAGS = -O2 -g -std=gnu11 -ffp-contract=off -DSP_INT_OVERFLOW_MODE_$(INT_OVERFLOW) \
         -I$(SRC) -I$(SPINEL)/lib -I$(SPINEL)/lib/regexp
LIBS = $(SPINEL)/lib/libspinel_rt.a -lm

m0: m0.c $(SRC)/lupe_tu.c $(SRC)/lupe_tu.h
	$(CC) $(CFLAGS) -o $@ m0.c $(LIBS)

run: m0
	./m0

clean:
	rm -f m0

.PHONY: run clean
