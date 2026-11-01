# Builds the engine with 'make', as OpenBench does (Linux, macOS, or WSL; it wraps CMake):
#
#     make EXE=<name> [CC=<compiler>] [EVALFILE=<network>] [TUNE=1]
#
#   EXE       the binary to create in this directory (default tiki)
#   CC/CXX    the compilers (default clang; CXX follows CC, clang-18 -> clang++-18, gcc -> g++)
#   EVALFILE  the network to build in (default the newest in models/), the binary runs on its own
#   TUNE=1    the SPSA tuning build: the parameters in engine/core/src/tune.h become UCI options,
#             and '<binary> tune' prints OpenBench's SPSA input for them
#
# Only the engine is built, in build-make/: no tests or benchmarks, so nothing is downloaded. The
# CPU features (AVX2, PEXT) are those of the machine building it, as the evaluations and bench are
# the same with and without them. Every run configures afresh, so no option is left over from an
# earlier one ('make clean' to change compiler).

EXE      ?= tiki
EVALFILE ?=
TUNE     ?= 1
BUILD    := build-make

ifeq ($(origin CC),default)
    CC := clang
endif
ifeq ($(origin CXX),default)
    CXX := $(subst clang,clang++,$(subst gcc,g++,$(CC)))
endif

ifeq ($(TUNE),1)
    TUNE_OPTION := ON
else
    TUNE_OPTION := OFF
endif

.PHONY: all clean

all:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=$(CC) -DCMAKE_CXX_COMPILER=$(CXX) \
	      -UTIKI_USE_AVX2 -UTIKI_USE_PEXT -DTIKI_EMBED_NETWORK=ON -DTIKI_EMBED_NETWORK_FILE="$(EVALFILE)" \
	      -DTIKI_TUNE=$(TUNE_OPTION) -DTIKI_BUILD_TESTS=OFF -DTIKI_BUILD_BENCHMARKS=OFF
	cmake --build $(BUILD) --target tiki_app --parallel
	cp $(BUILD)/engine/app/tiki $(EXE)

clean:
	rm -rf $(BUILD) $(EXE)
