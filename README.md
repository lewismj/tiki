## tiki

A UCI chess engine in C (C23), for Linux and other POSIX systems (on Windows, run it under WSL, see `scripts/`).

### Features

- **UCI**: `uci`, `isready`, `ucinewgame`, `position` (startpos / fen, with moves), `go` (`wtime`/`btime`/`winc`/`binc`,
  `movestogo`, `movetime`, `depth`, `infinite`), `stop`, `quit`, `setoption`.
  - Options: `Hash` (MB, default 256, max 65536; resizing clears the table) and `Threads` (default 1, max 256,
    used from the next `go`). Option names are case insensitive.
  - Input is read on its own thread. `stop`, `quit` and `isready` are handled during a search, and other commands
    are queued, so nothing is lost when a GUI sends several at once.
  - Not yet: pondering, MultiPV.

- **Move generation**: bitboards with PEXT (or magic bitboards) slider lookups and make/undo with incremental Zobrist
  hashing. It's quite fast: under 2 s for perft 6 from the start position. It's tested against 6K+ perft positions,
  and the incremental hash is checked against a full recompute at every node.

- **Search**: iterative deepening with aspiration windows (±25, widened on a fail), and PVS.
  - **Pruning and reductions**: null move pruning (with zugzwang and PV guards), reverse futility and futility
    pruning, log-based late move reductions, internal iterative reduction, and a check extension (including at the
    horizon).
  - **Quiescence**: searches captures and queen promotions, skips captures that lose material (SEE) and uses delta
    pruning.
  - **Move ordering**: TT move, PV, captures by MVV-LVA (losing captures last), killers, then history with gravity.
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
below as flags, e.g. `--embed --test` (see `scripts/README.md`, or `--help`).

The unit tests build a small material-counting network in code, so they don't need a network file. The bench smoke
test runs `tiki_app`, so it's skipped unless `TIKI_EVALFILE` is set or a network is built in.

- `-DTIKI_USE_PEXT=OFF` uses magic bitboards. PEXT is on by default when the build machine has BMI2; turn it off on
  AMD Zen 1/2, where PEXT is slow.
- `-DTIKI_USE_AVX2=OFF` uses plain C for the network instead of AVX2 (on by default when the build machine has
  AVX2). Only `nnue.c` is built with `-mavx2`, and both versions give exactly the same evaluations.
- `-DTIKI_SANITIZE=address,undefined` (or `thread`) builds everything with sanitizers.
- `-DTIKI_BUILD_BENCHMARKS=OFF` skips fetching and building Google Benchmark.
- `-DTIKI_EMBED_NETWORK=ON` builds a network into `tiki_app`, so it runs with no `.nnue` file, `TIKI_EVALFILE` or
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
over Win32, and is on the include path only for Windows builds. The CRT is linked statically, so `tiki_app.exe`
runs without the Visual C++ redistributable.

### Running

Every command except `nnue-eval` needs the network, `TIKI_EVALFILE=/mnt/f/tiki/nets/tiki-11.nnue` for example, and
exits with an error without one. A binary built with `-DTIKI_EMBED_NETWORK=ON` uses its built in network when
`TIKI_EVALFILE` isn't set. The UCI `EvalFile` option loads another network; its default is the network in use, and
for a built in one that's its name (`tiki-2026-10-23.nnue`), which selects it again.

| Command | |
|---|---|
| `build/engine/app/tiki_app` | UCI engine on stdin/stdout, for a GUI or `cutechess-cli` / `fastchess`. |
| `tiki_app bench [depth]` | Fixed benchmark, see below. |
| `tiki_app datagen <file> [games] [depth] [seed]` | Self-play NNUE training data. |
| `tiki_app nnue-eval <net> <fen>` | Evaluation of a position by a trained network. |

On Windows, `scripts/run_engine_wsl.bat` starts the engine in WSL for a Windows GUI (Fritz, Cute Chess, ...), see
`scripts/README.md`.

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

- `tiki_app bench [depth]` (or `bench [depth]` in the UCI loop) searches 16 fixed positions to a fixed depth
  (default 11), with a 16 MB TT cleared before each one, and prints the total node count and nodes/second.
  - The node count only changes when the search or evaluation does, so it's a signature for refactors that shouldn't
    change behaviour. It depends on the network too, so compare signatures with the same one: with
    `models/tiki-2026-10-23.nnue` it's **3,652,233** nodes.
  - Timings vary between runs and sessions, so only compare times from back-to-back runs.
- `build/engine/benchmark/tiki_benchmark` runs Google Benchmark micro-benchmarks of evaluation, move generation,
  make/undo, perft and a fixed-depth search. Use it to compare an evaluation change on its own.

### Testing strength

The bench shows whether behaviour changed, not whether tiki got stronger. For that, play matches with
`scripts/match.sh` (Linux, WSL, macOS) or `scripts\match.bat` (Windows), which download
[fastchess](https://github.com/Disservin/fastchess) and an opening book into `tools/` the first time:

```
scripts/match.sh sprt build/engine/app/tiki_app base/engine/app/tiki_app     # a change vs the build before it
scripts/match.sh gauntlet build/engine/app/tiki_app /path/to/engine1 /path/to/engine2
```

- `sprt` plays until a sequential probability ratio test decides whether the new build gains (bounds `[0, 5]`
  normalized Elo by default), usually a few thousand games at 8+0.08. Test every search change this way.
- `gauntlet` plays a fixed number of games (200) against each engine and reports the Elo differences. With engines
  of known CCRL ratings this places tiki on that scale.
- Settings come from the environment (`TC`, `CONCURRENCY`, `GAMES`, `ELO0`/`ELO1`, `NEW_EVALFILE`/`BASE_EVALFILE` to
  test networks, ...), see the top of either script. Games and results go to `matches/`.

### NNUE

The network, the training data format and the trainer are described in [`training/README.md`](training/README.md).
In short:

```
build/engine/app/tiki_app datagen data/d8-s1.txt 10000 8 1      # self-play: several processes, different seeds
python3 training/lichess.py --download-dir /mnt/f/lichess --out-dir /mnt/f/lichess/data --months 2024-01:2024-03
training/run_training.sh --data-dir data --data-dir /mnt/f/lichess/data --out training/nets   # tests, then train.py
build/engine/app/tiki_app nnue-eval training/nets/tiki-20.nnue "<fen>"
```

There are two data sources. Self-play (`datagen`) labels positions with tiki's own search. Lichess's analysed games
(`lichess.py`) are labelled with Stockfish evals: a stronger teacher than tiki's own search, but a net trained on
them learns Stockfish's judgement (see the training README).

The trainer's tests check its features and integer evaluation against the engine's
(`TIKI_APP=... python3 -m unittest discover -s tests`).

### Layout

| | |
|---|---|
| `engine/core/src` | The engine library: board, move generation, search (`search.c`), TT, UCI, time control, bench, datagen. |
| `engine/core/src/nnue` | Network inference, incremental accumulators and file format (`nnue.h` defines the features and quantisation). |
| `engine/core/include/core/tiki.h` | Public interface (startup, UCI loop, bench, datagen, nnue-eval). |
| `engine/app` | The `tiki_app` executable. |
| `engine/test` | Unity tests, run through CTest. |
| `engine/benchmark` | Google Benchmark micro-benchmarks (C++, through a C shim). |
| `engine/platform/win32` | POSIX headers over Win32, for Windows builds only. |
| `engine/magic_generator` | Generates the magic numbers for the magic bitboard lookups. |
| `training` | PyTorch NNUE trainer and its tests. |

### Next steps

- **Search:** each one checked with an SPRT (`scripts/match.bat` / `match.sh`):
  - late move pruning (skip late quiet moves at low depth);
  - a transposition table of four-entry buckets (one cache line) with depth and age replacement, and
    prefetching the child's entry after each move;
  - continuation history (a quiet move's history given the previous move);
  - singular extensions, then ProbCut.
- **Tuning:** the search margins are common starting values, not tuned. SPRT their changes, and measure the
  Lazy SMP gain at different thread counts.
- **Parallel search:** Lazy SMP is in. Next would be letting helpers vote on the best move (or take it from the
  deepest completed thread), and more structured parallelism after that.
- **Network:** training on lc0's data, output buckets and a wider first layer (see the training README).
