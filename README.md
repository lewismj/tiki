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

The unit tests build a small material-counting network in code, so they don't need a network file. The bench smoke
test runs `tiki_app`, so it's skipped unless `TIKI_EVALFILE` is set.

- `-DTIKI_USE_PEXT=OFF` uses magic bitboards. PEXT is on by default when the build machine has BMI2; turn it off on
  AMD Zen 1/2, where PEXT is slow.
- `-DTIKI_USE_AVX2=OFF` uses plain C for the network instead of AVX2 (on by default when the build machine has
  AVX2). Only `nnue.c` is built with `-mavx2`, and both versions give exactly the same evaluations.
- `-DTIKI_SANITIZE=address,undefined` (or `thread`) builds everything with sanitizers.
- `-DTIKI_BUILD_BENCHMARKS=OFF` skips fetching and building Google Benchmark.
- Use Release (or RelWithDebInfo). Debug (`-O0`) doesn't build yet, because some `always_inline` functions are
  called through function pointers in the move generator.

### Running

Every command except `nnue-eval` needs the network, `TIKI_EVALFILE=/mnt/f/tiki/nets/tiki-11.nnue` for example, and
exits with an error without one.

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
(`x86-64-bmi2`, `x86-64-avx2`, `x86-64`). Each unpacks to `tiki-v<version>/` with `bin/tiki` (statically
linked, so it runs on any x86-64 Linux), the networks from `models/`, and `scripts/run.sh`, which starts tiki with
the last network in `models/` by name (set `TIKI_EVALFILE` to override). Running the workflow by hand builds the
packages as a workflow artifact without releasing.

`scripts/package.sh <variant> [cmake options]` builds, tests and packages one variant locally, into `dist/`.

### Benchmarking

- `tiki_app bench [depth]` (or `bench [depth]` in the UCI loop) searches 16 fixed positions to a fixed depth
  (default 11), with a 16 MB TT cleared before each one, and prints the total node count and nodes/second.
  - The node count only changes when the search or evaluation does, so it's a signature for refactors that shouldn't
    change behaviour. It depends on the network too, so compare signatures with the same one: with
    `models/tiki-2026-10-23.nnue` it's **3,652,233** nodes.
  - Timings vary between runs and sessions, so only compare times from back-to-back runs.
- `build/engine/benchmark/tiki_benchmark` runs Google Benchmark micro-benchmarks of evaluation, move generation,
  make/undo, perft and a fixed-depth search. Use it to compare an evaluation change on its own.

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
| `engine/magic_generator` | Generates the magic numbers for the magic bitboard lookups. |
| `training` | PyTorch NNUE trainer and its tests. |

### Next steps

- **A built-in network:** embed the `.nnue` file in the binary at build time, loaded by `load_startup_network`
  (`tiki.h`), so `TIKI_EVALFILE` becomes optional.
- **Strength testing:** a match setup (SPRT) to check search changes and tune their margins, and to measure the
  Lazy SMP gain at different thread counts. The current margins are common starting values, not tuned.
- **Parallel search:** Lazy SMP is in. Next would be letting helpers vote on the best move (or take it from the
  deepest completed thread), and more structured parallelism after that.
