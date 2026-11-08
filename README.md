# tiki

<div align="center" style="display: flex; justify-content: center; align-items: center; gap: 20px;">
  <img src="docs/img/tiki.png" alt="Tiki logo" width="90">
</div>

A UCI chess engine in C (C23) with its own NNUE evaluation, for Linux, and Windows.

## Strength

Not on a rating list yet. Gauntlets with one thread each, UHO openings:

| Opponent     | Time control               | Build and network                     | Games | Score | Elo      |
| ------------ | -------------------------- | ------------------------------------- | ----- | ----- | -------- |
| Leorik 3.2.1 | 8+0.08                     | 8 Nov 2026, `tiki-2026-11-08.nnue`    | 300   | 62.7% | +90 ± 28 |
| Leorik 3.2.1 | 90+0.75 (about CCRL Blitz) | 8 Nov 2026, `tiki-2026-11-08.nnue`    | 60    | 60.0% | +70 ± 49 |

## Network

```text
HalfKAv2_hm (22528) -> 512 x 2 -> pairwise products (256 x 2) -> [16 -> CReLU -> 1] x 32 layer stacks
```

The accumulator's halves are multiplied pairwise into 512 inputs for a small hidden layer, one of 32 layer stacks chosen by the number of pieces and which sides have a queen. Trained with tiki's own PyTorch trainer on 1.95B positions from Lichess games between players rated 2000+, labelled with Lichess's Stockfish evaluations and the game results. There's no hand-crafted evaluation. See
[docs/nnue.md](docs/nnue.md).

## Features

- **Search:** PVS with aspiration windows, a staged move picker, history, capture and continuation histories,
  correction history, null move, futility, late move and SEE pruning, late move reductions, singular
  extensions, and a shared bucketed transposition table with Lazy SMP.
- **Tablebases:** Syzygy probing in the search and at the root (through Fathom).
- **Evaluation:** NNUE with incremental, lazy accumulator updates, a refresh cache per king square, and SIMD
  code for each CPU level (SSE2 to AVX-512 VNNI).
- **Time management:** soft and hard limits, scaled by how stable the best move is.
- **Move generation:** bitboards with PEXT or magic lookups, tested against 6K+ perft positions.

Details in [docs/engine.md](docs/engine.md).

## Building

Needs CMake 3.24+ and Clang or GCC with C23 (on Windows, LLVM clang with the Visual Studio SDK).

```bash
make                                   # ./tiki, with the newest network in models/ built in
```

or, with the tests:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DTIKI_EMBED_NETWORK=ON
cmake --build build && (cd build && ctest -LE slow)
```

Options, Windows, and releases: [docs/building.md](docs/building.md).

## Usage

Any UCI GUI or tester can run `tiki` (`tiki.exe`) directly.

| UCI option         | Default   |                                                                                                   |
| ------------------ | --------- | ------------------------------------------------------------------------------------------------- |
| `Hash`             | 256       | transposition table, MB (1 to 65536)                                                              |
| `Threads`          | 1         | search threads (1 to 256)                                                                         |
| `Move Overhead`    | 30        | ms kept back from each move for GUI and transport delays                                          |
| `EvalFile`         | built in  | another network (`.nnue`)                                                                         |
| `SyzygyPath`       | `<empty>` | Syzygy tablebase directories, separated by `;` on Windows and `:` elsewhere                       |
| `SyzygyProbeDepth` | 1         | the depth from which the search probes positions with as many pieces as the largest tables        |
| `SyzygyProbeLimit` | 7         | probe positions with at most this many pieces (5 for tables on slow storage, such as a USB drive) |

| Command                                      |                                                                                        |
| -------------------------------------------- | -------------------------------------------------------------------------------------- |
| `tiki bench [depth]`                         | fixed benchmark: node count (a signature of the search and network) and speed          |
| `tiki datagen <file> [games] [depth] [seed]` | self-play training data: the trainer's binary format for a `.bin` file, text otherwise |
| `tiki nnue-eval <net> <fen>`                 | a network's evaluation of a position                                                   |

## Documentation

|                                        |                                                                                          |
| -------------------------------------- | ---------------------------------------------------------------------------------------- |
| [docs/engine.md](docs/engine.md)       | UCI, move generation, search, time management, evaluation                                |
| [docs/building.md](docs/building.md)   | build options, Windows, OpenBench's `make`, releases                                     |
| [docs/testing.md](docs/testing.md)     | bench, SPRTs and gauntlets, OpenBench and SPSA tuning                                    |
| [docs/nnue.md](docs/nnue.md)           | the network, its data and training (details in [training/README.md](training/README.md)) |
| [docs/roadmap.md](docs/roadmap.md)     | what's next                                                                              |
| [scripts/README.md](scripts/README.md) | the build, match and packaging scripts                                                   |

## Credits

- Maksim Korzh's Chess Engine in C (YouTube series), learning how Chess engines are built and core concepts.
- The [Lichess open database](https://database.lichess.org/) (CC0): the games and their Stockfish evaluations that
  tiki's network is initially trained on.
- [Fathom](https://github.com/jdart1/Fathom) (MIT licence) by Ronald de Man and Jon Dart, for probing
  Syzygy tablebases (`engine/third_party/fathom`).
- [Leorik](https://github.com/lithander/Leorik) by Thomas Jahn: a reference to how a modern engine is structured
  and written, studied not copied, different search and evaluation, and a benchmark opponent for tiki.

## Licence

GPL v3, see [LICENSE](LICENSE).
