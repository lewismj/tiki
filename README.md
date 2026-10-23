## tiki

A UCI chess engine in C (C23), for Linux and other POSIX systems (on Windows, run it under WSL, see `scripts/`).

### Features

- **UCI**: `uci`, `isready`, `ucinewgame`, `position` (startpos / fen, with moves), `go` (`wtime`/`btime`/`winc`/`binc`,
  `movestogo`, `movetime`, `depth`, `infinite`), `stop`, `quit`.
  - Input is read on its own thread. `stop`, `quit` and `isready` are handled during a search, and other commands
    are queued, so nothing is lost when a GUI sends several at once.
  - Not yet: `setoption` (the hash is fixed at 256 MB), pondering, MultiPV.

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
  - **Transposition table**: 16-byte entries that store the best move, with generation-based replacement. It's kept
    between moves and cleared on `ucinewgame`.
  - **Draws**: repetition (game history plus search path), the fifty-move rule and insufficient material.

- **Time control**: hard and soft limits. A new depth isn't started after half the time allocated to the move.

- **Evaluation**: hand-crafted for now. Tiki's own HalfKAv2_hm network (22528 → 256×2 → 1) has inference, self-play
  data generation and a PyTorch trainer (see [NNUE](#nnue)), but isn't used in search yet.

### Building and testing

Needs CMake 3.24+ and GCC or Clang with C23. Unity (the C tests) and Google Benchmark are fetched at configure time.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && ctest -LE slow        # unit tests and bench smoke test; 'ctest' also runs the full perft file
```

- `-DTIKI_USE_PEXT=OFF` uses magic bitboards. PEXT is on by default when the build machine has BMI2; turn it off on
  AMD Zen 1/2, where PEXT is slow.
- `-DTIKI_SANITIZE=address,undefined` (or `thread`) builds everything with sanitizers.
- `-DTIKI_BUILD_BENCHMARKS=OFF` skips fetching and building Google Benchmark.
- Use Release (or RelWithDebInfo). Debug (`-O0`) doesn't build yet, because some `always_inline` functions are
  called through function pointers in the move generator.

### Running

| Command | |
|---|---|
| `build/engine/app/tiki_app` | UCI engine on stdin/stdout, for a GUI or `cutechess-cli` / `fastchess`. |
| `tiki_app bench [depth]` | Fixed benchmark, see below. |
| `tiki_app datagen <file> [games] [depth] [seed]` | Self-play NNUE training data. |
| `tiki_app nnue-eval <net> <fen>` | Evaluation of a position by a trained network. |

On Windows, `scripts/run_engine.bat.example` starts the engine in WSL for a Windows GUI (Fritz, Cute Chess, ...).

### Benchmarking

- `tiki_app bench [depth]` (or `bench [depth]` in the UCI loop) searches 16 fixed positions to a fixed depth
  (default 11), with a 16 MB TT cleared before each one, and prints the total node count and nodes/second.
  - The node count only changes when the search or evaluation does, so it's a signature for refactors that shouldn't
    change behaviour. The current signature is **4,791,167** nodes.
  - Timings vary between runs and sessions, so only compare times from back-to-back runs.
- `build/engine/benchmark/tiki_benchmark` runs Google Benchmark micro-benchmarks of evaluation, move generation,
  make/undo, perft and a fixed-depth search. Use it to compare an evaluation change on its own.

### NNUE

The network, the training data format and the trainer are described in [`training/README.md`](training/README.md).
In short:

```
build/engine/app/tiki_app datagen data/d8-s1.txt 10000 8 1      # several processes, different seeds
cd training && python3 train.py --data ../data/*.txt --out nets  # PyTorch, CUDA if available
build/engine/app/tiki_app nnue-eval training/nets/tiki-20.nnue "<fen>"
```

The trainer's tests check its features and integer evaluation against the engine's
(`TIKI_APP=... python3 -m unittest discover -s tests`).

### Layout

| | |
|---|---|
| `engine/core/src` | The engine library: board, move generation, search (`search.c`), TT, UCI, time control, bench, datagen. |
| `engine/core/src/hce` | Hand-crafted evaluation. |
| `engine/core/src/nnue` | Network inference and file format (`nnue.h` defines the features and quantisation). |
| `engine/core/include/core/tiki.h` | Public interface (startup, UCI loop, bench, datagen, nnue-eval). |
| `engine/app` | The `tiki_app` executable. |
| `engine/test` | Unity tests, run through CTest. |
| `engine/benchmark` | Google Benchmark micro-benchmarks (C++, through a C shim). |
| `engine/magic_generator` | Generates the magic numbers for the magic bitboard lookups. |
| `training` | PyTorch NNUE trainer and its tests. |

### Next steps

- **Use the network in search.** Incremental accumulators in make/undo, an `EvalFile` option and SIMD, then a
  match against the hand-crafted eval.
- **UCI options:** `setoption` for `Hash`, and later `Threads`.
- **Strength testing:** a match setup (SPRT) to check search changes and tune their margins. The current margins are
  common starting values, not tuned.
- **Parallel search:** even just non-deterministically filling the transposition table (lazy SMP) would help, and it's
  a useful prototype before more structured parallelism.
