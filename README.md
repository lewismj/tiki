## tiki

A UCI chess engine in C (C23), for Linux and other POSIX systems and for Windows (built natively with LLVM clang, see
[Windows](#windows)).

### Features

- **UCI**: `uci`, `isready`, `ucinewgame`, `position` (startpos / fen, with moves), `go` (`wtime`/`btime`/`winc`/`binc`,
  `movestogo`, `movetime`, `depth`, `infinite`), `stop`, `quit`, `setoption`.
  - Options: `Hash` (MB, default 256, max 65536; resizing clears the table), `Threads` (default 1, max 256,
    used from the next `go`), `Move Overhead` (ms kept back from each move for GUI and transport delays, default
    30, max 5000) and `EvalFile` (the network, see [Running](#running)). Option names are case insensitive.
  - Input is read on its own thread. `stop`, `quit` and `isready` are handled during a search, and other commands
    are queued, so nothing is lost when a GUI sends several at once.
  - Not yet: pondering, MultiPV.

- **Move generation**: bitboards with PEXT (or magic bitboards) slider lookups and make/undo with incremental Zobrist
  hashing. It's quite fast: under 2 s for perft 6 from the start position. It's tested against 6K+ perft positions,
  and the incremental hash is checked against a full recompute at every node.

- **Search**: iterative deepening with aspiration windows (±25, widened on a fail), and PVS.
  - **Pruning and reductions**: null move pruning (with zugzwang and PV guards), reverse futility and futility
    pruning, late move pruning, SEE pruning of losing moves at low depth, log-based late move reductions, internal
    iterative reduction, and a check extension (including at the horizon).
  - **Quiescence**: searches captures and queen promotions, skips captures that lose material (SEE) and uses delta
    pruning.
  - **Move ordering**: TT move, PV, captures by MVV-LVA (losing captures last), killers, then history. History
    rises by min(16 × depth², 1600) when a quiet move raises alpha and falls by the same for the quiet moves tried
    before a quiet cutoff, with gravity keeping it under the killers. It's cleared before each search.
  - **Transposition table**: lock-less 16-byte entries (Hyatt's XOR scheme, so torn entries read as misses) that
    store the best move, with generation-based replacement. It's kept between moves and cleared on `ucinewgame`.
  - **Lazy SMP**: with `Threads` > 1, helper threads search the same root on their own board and search state,
    sharing only the transposition table. Odd-numbered helpers search one ply deeper, to spread the threads over
    more of the tree. The main thread checks the clock, reports the total nodes (and nps) in `info` and picks
    the move.
  - **Draws**: repetition (game history plus search path), the fifty-move rule and insufficient material.

- **Time control**: hard and soft limits. A new depth isn't started after half the time allocated to the move.

- **Evaluation**: tiki's own HalfKAv2_hm network (22528 → 256×2 → 1), see [NNUE](#nnue). There's no hand-crafted
  evaluation, so tiki needs a network to run: set `TIKI_EVALFILE` to a `.nnue` file (the `EvalFile` UCI option
  changes it at runtime). The accumulators are updated incrementally along the search path, lazily: only when a
  position is evaluated. When a king moves, its side is refreshed through a cache of accumulators per king square
  ("Finny tables"), so only the pieces that differ from the cached position are applied. The accumulator updates
  and the output layer use AVX2 when it's built in.

### Building and testing

Needs CMake 3.24+ and GCC or Clang with C23. Unity (the C tests) and Google Benchmark are fetched at configure time.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && ctest -LE slow        # unit tests and bench smoke test; 'ctest' also runs the full perft file
```

Or `scripts/build-linux-wsl.sh` (Linux or WSL) and `scripts\build-windows.bat` (Windows), which take the options
below as flags, e.g. `--embed --test` (see `scripts/README.md`, or `--help`). `make` builds just the engine with
the network in, as OpenBench does (see [OpenBench](#openbench)).

The unit tests build a small material-counting network in code, so they don't need a network file. The bench smoke
test runs `tiki`, so it's skipped unless `TIKI_EVALFILE` is set or a network is built in.

- `-DTIKI_USE_PEXT=OFF` uses magic bitboards. PEXT is on by default when the build machine has BMI2; turn it off on
  AMD Zen 1/2, where PEXT is slow.
- `-DTIKI_USE_AVX2=OFF` uses plain C for the network instead of AVX2 (on by default when the build machine has
  AVX2). Only `nnue.c` is built with `-mavx2`, and both versions give exactly the same evaluations.
- `-DTIKI_SANITIZE=address,undefined` (or `thread`) builds everything with sanitizers.
- `-DTIKI_SEARCH_STATS=ON` counts move ordering statistics in the search (beta cutoffs, first move and quiet
  cutoffs), printed by `bench`. Off by default, so a normal build doesn't count them.
- `-DTIKI_BUILD_BENCHMARKS=OFF` skips fetching and building Google Benchmark.
- `-DTIKI_BUILD_TESTS=OFF` skips the unit tests, and fetching Unity for them.
- `-DTIKI_TUNE=ON` makes the tunable search parameters UCI options, for SPSA (see [OpenBench](#openbench)).
- `-DTIKI_EMBED_NETWORK=ON` builds a network into `tiki`, so it runs with no `.nnue` file, `TIKI_EVALFILE` or
  run script (for tournaments, or a GUI that only accepts an executable). It's the newest network in `models/` by
  the date in its name (`tiki-2026-11-05.nnue` over `tiki-2026-10-23.nnue`; git doesn't keep file times), or
  `-DTIKI_EMBED_NETWORK_FILE=<path>`. CMake picks up a new network in `models/` on the next build. The binary grows by
  the network's size (about 11.5 MB), and evaluates exactly as with the file (the same bench signature).
- Use Release (or RelWithDebInfo). Debug (`-O0`) doesn't build yet, because some `always_inline` functions are
  called through function pointers in the move generator.

#### Windows

Builds with LLVM clang targeting the MSVC ABI (`clang`/`clang++`, not `cl` or `clang-cl`), using the Visual Studio
Windows SDK and linker (tested with Visual Studio 2026 and LLVM 22), and Ninja (Visual Studio ships one). The bench signature matches the Linux build.

```
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build-win
```

`scripts\build-windows.bat` does this (finding Visual Studio's CMake and Ninja if they aren't on the `PATH`), e.g.
`scripts\build-windows.bat --embed` for an exe that runs on its own.

`engine/platform/win32` provides the POSIX headers tiki uses (`pthread.h`, `unistd.h`, `strings.h`, `sys/time.h`)
over Win32, and is on the include path only for Windows builds. The CRT is linked statically, so `tiki.exe`
runs without the Visual C++ redistributable.

### Running

Every command except `nnue-eval` needs the network, `TIKI_EVALFILE=/mnt/f/tiki/nets/tiki-11.nnue` for example, and
exits with an error without one. A binary built with `-DTIKI_EMBED_NETWORK=ON` uses its built in network when
`TIKI_EVALFILE` isn't set. The UCI `EvalFile` option loads another network; its default is the network in use, and
for a built in one that's its name (`tiki-2026-10-26.nnue`), which selects it again.

| Command | |
|---|---|
| `build/engine/app/tiki` | UCI engine on stdin/stdout, for a GUI or `cutechess-cli` / `fastchess`. |
| `tiki bench [depth]` | Fixed benchmark, see below. |
| `tiki datagen <file> [games] [depth] [seed]` | Self-play NNUE training data. |
| `tiki nnue-eval <net> <fen>` | Evaluation of a position by a trained network. |

On Windows, a GUI (Arena, Fritz, Cute Chess, ...) can run `tiki.exe` directly; one built with the network in needs
no other files. `scripts/run_engine_wsl.bat` instead starts the Linux build in WSL, see `scripts/README.md`.

### Releases

Pushing a tag `v<version>` matching `ENGINE_VERSION` in `engine/core/src/version.h` (e.g. `v0.0.1`) runs
`.github/workflows/release.yml`, which publishes a GitHub release with a package per CPU variant
(`x86-64-bmi2`, `x86-64-avx2`, `x86-64`) and platform. Each Linux package unpacks to `tiki-v<version>/` with
`bin/tiki` (statically linked, so it runs on any x86-64 Linux), the networks from `models/`, and `scripts/run.sh`,
which starts tiki with the last network in `models/` by name (set `TIKI_EVALFILE` to override). The Windows packages
(`.zip`) hold `bin/tiki.exe` (the CRT linked statically, it needs only Windows' own DLLs) and `scripts/run.bat`,
which does the same. The release is only published if all six packages give the same bench signature. Running the
workflow by hand builds the packages as a workflow artifact without releasing.

The network can also be built into the release binaries (`-DTIKI_EMBED_NETWORK=ON`), so `bin/tiki` and
`bin/tiki.exe` run on their own: tick 'embed_network' when running the workflow by hand, and for tag pushes set the
repository variable `TIKI_EMBED_NETWORK` to `true` (Settings > Secrets and variables > Actions > Variables).

`scripts/package.sh <variant> [cmake options]` builds, tests and packages one variant locally, into `dist/`, and
`scripts/package_windows.sh` does the same on Windows (from Git Bash). Add `-DTIKI_EMBED_NETWORK=ON` to the options
to build the network in.

### Benchmarking

- `tiki bench [depth]` (or `bench [depth]` in the UCI loop) searches 16 fixed positions to a fixed depth
  (default 11), with a 16 MB TT cleared before each one, and prints the total node count and nodes/second.
  - The node count only changes when the search or evaluation does, so it's a signature for refactors that shouldn't
    change behaviour. It depends on the network too, so compare signatures with the same one: with
    `models/tiki-2026-10-26.nnue` it's **1,389,650** nodes.
  - Timings vary between runs and sessions, so only compare times from back-to-back runs.
- `build/engine/benchmark/tiki_benchmark` runs Google Benchmark micro-benchmarks of evaluation, move generation,
  make/undo, perft and a fixed-depth search. Use it to compare an evaluation change on its own.

### Testing strength

The bench shows whether behaviour changed, not whether tiki got stronger. For that, play matches with
`scripts/match.sh` (Linux, WSL, macOS) or `scripts\match.bat` (Windows), which download
[fastchess](https://github.com/Disservin/fastchess) and an opening book into `tools/` the first time:

```
scripts/match.sh sprt build/engine/app/tiki base/engine/app/tiki     # a change vs the build before it
scripts/match.sh gauntlet build/engine/app/tiki /path/to/engine1 /path/to/engine2
```

- `sprt` plays until a sequential probability ratio test decides whether the new build gains (bounds `[0, 5]`
  normalized Elo by default), usually a few thousand games at 8+0.08. Test every search change this way.
- `gauntlet` plays a fixed number of games (200) against each engine and reports the Elo differences. With engines
  of known CCRL ratings this places tiki on that scale.
- Settings come from the environment (`TC`, `CONCURRENCY`, `GAMES`, `ELO0`/`ELO1`, `NEW_EVALFILE`/`BASE_EVALFILE` to
  test networks, ...), see the top of either script. Games and results go to `matches/`.

#### OpenBench

Tiki builds the way [OpenBench](https://github.com/AndyGrant/OpenBench) expects, with the `Makefile` in the
repository root (Linux, macOS or WSL; it wraps CMake):

```
make EXE=tiki-dev [CC=clang] [EVALFILE=path/to/net.nnue] [TUNE=1]
./tiki-dev bench                # '... Nodes searched : N ... Nodes/second : N', as OpenBench reads it
```

It builds only the engine (no tests or benchmarks, so nothing is downloaded), with the network built in:
`EVALFILE`, which OpenBench passes for a network uploaded to it, or the newest in `models/`. The engine
config for an OpenBench server is `scripts/openbench/Tiki.json`: copy it to the server's `Engines/` and set
`nps` to the `Nodes/second` of `bench` on the reference machine.

**SPSA tuning.** `TUNE=1` (CMake `-DTIKI_TUNE=ON`) makes the search parameters in `engine/core/src/tune.h`
variables, each with a UCI option of its name (`RFP_MARGIN`, `FUTILITY_MARGIN`, `LMP_BASE`, `SE_MARGIN`, ...),
and `tiki tune` prints OpenBench's SPSA input for them (name, type, value, min, max, C_end, R_end). A normal
build keeps them as constants. OpenBench can't pass `TUNE=1`, so a tune runs from a branch that has it:

1. Make a branch with `TUNE ?= 1` in the `Makefile`, and push it.
2. `make TUNE=1 && ./tiki tune`, and paste the lines into OpenBench's SPSA input for that branch.
3. When the tune ends, copy the values into `engine/core/src/search_constants.h` on `main`, and test the
   result with an SPRT against `main` before keeping it.

### NNUE

The network, the training data format and the trainer are described in [`training/README.md`](training/README.md).
In short:

```
build/engine/app/tiki datagen data/d8-s1.txt 10000 8 1      # self-play: several processes, different seeds
python3 training/lichess.py --download-dir /mnt/f/lichess --out-dir /mnt/f/lichess/data --months 2024-01:2024-03
training/run_training.sh --data-dir data --data-dir /mnt/f/lichess/data --out training/nets   # tests, then train.py
build/engine/app/tiki nnue-eval training/nets/tiki-20.nnue "<fen>"
```

There are three data sources. Self-play (`datagen`) labels positions with tiki's own search. Lichess's analysed
games (`lichess.py`) are labelled with Stockfish evals: a stronger teacher than tiki's own search, but a net trained
on them learns Stockfish's judgement (see the training README). Leela Chess Zero's self-play (`lc0.py`) is labelled
with lc0's own evaluation, under the ODbL.

The current network, `models/tiki-2026-10-26.nnue`, was trained on 650M positions from a year of Lichess games
between players rated 2000+, with a cosine learning rate schedule. Its recipe, and the SPRT against the network
before it (+121 Elo), are in the training README under "What worked".

The trainer's tests check its features and integer evaluation against the engine's
(`TIKI_APP=... python3 -m unittest discover -s tests`).

### Layout

| | |
|---|---|
| `engine/core/src` | The engine library: board, move generation, search (`search.c`), TT, UCI, time control, bench, datagen. |
| `engine/core/src/nnue` | Network inference, incremental accumulators and file format (`nnue.h` defines the features and quantisation). |
| `engine/core/include/core/tiki.h` | Public interface (startup, UCI loop, bench, datagen, nnue-eval). |
| `engine/app` | The `tiki` executable. |
| `engine/test` | Unity tests, run through CTest. |
| `engine/benchmark` | Google Benchmark micro-benchmarks (C++, through a C shim). |
| `engine/platform/win32` | POSIX headers over Win32, for Windows builds only. |
| `engine/magic_generator` | Generates the magic numbers for the magic bitboard lookups. |
| `training` | PyTorch NNUE trainer and its tests. |

### Next steps

- **Search:** each one checked with an SPRT (`scripts/match.bat` / `match.sh`):
  - singular extensions and multi-cut, then ProbCut;
  - the countermove heuristic (the best reply to the opponent's last move, ordered between the killers and
    history);
  - a transposition table of four-entry buckets (one cache line) with depth and age replacement, and
    prefetching the child's entry after each move.
  - Continuation history was tried and taken out again (commit `2aaadcb`).
- **Endgame tablebases:** Syzygy probing (`SyzygyPath`), through Fathom. Rating lists give engines tablebases.
- **Tuning:** the search margins are common starting values, not tuned. SPRT their changes, and measure the
  Lazy SMP gain at different thread counts.
- **Parallel search:** Lazy SMP is in. Next would be letting helpers vote on the best move (or take it from the
  deepest completed thread), and more structured parallelism after that.
- **Network:** output buckets by piece count, then a wider first layer; mixing in lc0's or self-play data (see the
  training README).
