# syzygy_relabel: Syzygy tablebase results for tiki's training data (see syzygy_relabel.c). Linux / WSL:
#
#     make            builds ./syzygy_relabel (downloads Fathom, the tablebase prober, into build/)
#     make test TB=<dir of .rtbw tables>
#                     checks it against python-chess's own Syzygy prober (tests/compare.py)
#
# Fathom (https://github.com/jdart1/Fathom, MIT licence) is fetched at a fixed commit, not kept in the
# repository; build/ is ignored by git.

CC      ?= clang
ifeq ($(origin CC),default)
    CC := clang
endif
CFLAGS  ?= -O3 -march=native
CFLAGS  += -std=gnu2x -DNDEBUG -pthread -Wall -Wextra

FATHOM_COMMIT := c9c6fef0dddc05d2e242c183acf5833149ab676d
FATHOM_URL    := https://github.com/jdart1/Fathom/archive/$(FATHOM_COMMIT).tar.gz
FATHOM        := build/Fathom-$(FATHOM_COMMIT)/src

TB      ?=

syzygy_relabel: syzygy_relabel.c build/libfathom.a
	$(CC) $(CFLAGS) -I$(FATHOM) -o $@ syzygy_relabel.c build/libfathom.a -pthread

build/libfathom.a: $(FATHOM)/tbprobe.c
	$(CC) -O3 -march=native -std=gnu11 -DNDEBUG -pthread -w -I$(FATHOM) -c -o build/tbprobe.o $(FATHOM)/tbprobe.c
	ar rcs $@ build/tbprobe.o

$(FATHOM)/tbprobe.c:
	mkdir -p build
	curl -fsSL $(FATHOM_URL) | tar -xz -C build
	touch $@

test: syzygy_relabel
	@test -n "$(TB)" || { echo "make test TB=<directory of Syzygy .rtbw tables>"; exit 2; }
	python3 tests/compare.py ./syzygy_relabel --tb $(TB)

clean:
	rm -rf build syzygy_relabel

.PHONY: test clean
