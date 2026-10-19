# Migration corpus build and test harness.  Plain C by default; the
# transformed tree is built with "make test CC=<crugc> CFLAGS=<flags>".
#
# Executables are written to ../build/migration (a sibling of this directory),
# so a checkout stays clean, this directory holds only sources, the sidecar,
# the Makefile and the expected output, and a copied corpus tree builds into
# its own sibling build directory.

CC ?= cc
CFLAGS ?=
PROGS = 01-heap 02-string 03-file 04-branch 05-unknown 06-alias 07-mixed 08-params
BIN = ../build/migration

all: $(PROGS)

$(PROGS): %: %.c
	@mkdir -p $(BIN)
	$(CC) $(CFLAGS) -o $(BIN)/$@ $<

test: all
	@for p in $(PROGS); do $(BIN)/$$p | diff -u expected/$$p.out - || exit 1; done
	@echo "migration: $(words $(PROGS)) programs pass"

clean:
	rm -rf $(BIN) migration-sample.txt
