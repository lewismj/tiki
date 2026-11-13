# Testing

## Bench

`tiki bench [depth]` (or `bench` in the UCI loop) searches 16 fixed positions to a fixed depth (default 11), with
a 16 MB TT cleared before each, and prints the total nodes and nodes per second.

- The node count changes only when the search or the network does, so it's a signature for changes that shouldn't
  change behaviour. With `models/tiki-2026-11-08.nnue` it's **1,110,079** (repetition rule and en passant hash fixes), the same
  at every CPU level.
- Timings vary between runs, so only compare times from back-to-back runs on an idle machine.
- `build/engine/benchmark/tiki_benchmark` runs Google Benchmark micro-benchmarks of evaluation, move generation,
  make/undo, perft and a fixed-depth search.

## Matches

The bench shows whether behaviour changed, not whether tiki got stronger. For that, `scripts/match.sh` (Linux,
WSL, macOS) and `scripts\match.bat` (Windows) play matches with [fastchess](https://github.com/Disservin/fastchess),
downloading it and the UHO opening book into `tools/` the first time:

```bash
scripts/match.sh sprt build/engine/app/tiki base/engine/app/tiki     # a change against the build before it
scripts/match.sh gauntlet build/engine/app/tiki /path/to/engine1 /path/to/engine2
```

- **`sprt`** plays until a sequential probability ratio test decides whether the new build gains (bounds `[0, 5]`
  normalised Elo), usually a few thousand games at 8+0.08. Test search changes this way, several related ones
  together rather than each alone.
- **`gauntlet`** plays a fixed number of games (200) against each engine and reports the Elo differences.
- Settings come from the environment: `TC`, `CONCURRENCY`, `GAMES`, `ELO0`/`ELO1`, `HASH`, `BOOK`, and
  `NEW_EVALFILE`/`BASE_EVALFILE` to test networks (see the top of either script). Games and results go to
  `matches/`.

## OpenBench

Copy `scripts/openbench/Tiki.json` to the server's `Engines/` and set `nps` to the `Nodes/second` of `bench` on the
reference machine. Workers build with the root `Makefile` ([building.md](building.md#make-openbench)).

## SPSA tuning

`TUNE=1` (CMake `-DTIKI_TUNE=ON`) turns the search parameters in `engine/core/src/tune.h` into variables, each with a
UCI option of its name (`RFP_MARGIN`, `LMR_BASE`, `TM_SOFT_PERCENT`, ...), and `tiki tune` prints OpenBench's SPSA
input for them (name, type, value, min, max, C_end, R_end). A normal build keeps them as constants. OpenBench can't
pass `TUNE=1`, so a tune runs from a branch that has it:

1. Make a branch with `TUNE ?= 1` in the `Makefile`, and push it.
2. `make TUNE=1 && ./tiki tune`, and paste the lines into OpenBench's SPSA input (without a trailing empty line,
   which OpenBench rejects as malformed).
3. When it ends, copy the values into `engine/core/src/search_constants.h` (the time management ones into
   `limits.h`) on `main`, and SPRT the result against `main`.

## Unit tests

`ctest` runs the Unity tests (`engine/test`): the board, make/undo and perft, repetitions, the search, the TT,
UCI and its input thread, the network's file format and evaluation, Syzygy probing, and the bench smoke test.

The Syzygy tests that need tables are ignored unless `TIKI_SYZYGY_PATH` points at them (at least the 3-5 piece
WDL and DTZ tables), e.g. `TIKI_SYZYGY_PATH=/mnt/f/syzygy/wdl:/mnt/f/syzygy/dtz ctest`. Their expected results
come from python-chess's own prober. The trainer has its own tests,
which also check its features and integer evaluation against the engine's ([training/README.md](../training/README.md#tests)).
