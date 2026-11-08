# lichess_convert: Lichess dumps to training data (see lichess_convert.c). Linux / WSL:
#
#     make            builds ./lichess_convert (needs libzstd.so.1, the zstd library; no headers needed)
#     make test       checks it on the small PGNs in tests/ against python-chess (lichess.py's reader)

CORE    := ../../engine/core/src
CC      ?= clang
ifeq ($(origin CC),default)
    CC := clang
endif
CFLAGS  ?= -O3 -march=native
CFLAGS  += -std=gnu2x -DNDEBUG -pthread -I$(CORE) -Wall -Wextra -Wno-unused-parameter
ZSTD    := $(if $(wildcard /usr/include/zstd.h),-lzstd,-l:libzstd.so.1)
SOURCES := lichess_convert.c $(addprefix $(CORE)/,types.c bitboard_constants.c bitboard_ops.c mask_generator.c \
           random.c attack_mask.c move_encoding.c zobrist_key.c board.c move_generator.c)

lichess_convert: $(SOURCES) $(wildcard $(CORE)/*.h)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) $(ZSTD) -pthread

test: lichess_convert
	python3 tests/compare.py ./lichess_convert

clean:
	rm -f lichess_convert

.PHONY: test clean
