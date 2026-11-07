# The engine

The search parameters below are the defaults in `engine/core/src/search_constants.h` (time management in
`limits.h`); most are tunable for SPSA (see [testing.md](testing.md#spsa-tuning)).

## UCI

- Commands: `uci`, `isready`, `ucinewgame`, `position` (startpos / fen, with moves), `go` (`wtime`/`btime`/`winc`/
  `binc`, `movestogo`, `movetime`, `depth`, `nodes`, `infinite`), `stop`, `quit`, `setoption`, and `bench`.
- Options: `Hash`, `Threads`, `Move Overhead`, `EvalFile`, `SyzygyPath` and `SyzygyProbeDepth` (see the
  [README](../README.md#usage)). Names are case insensitive. Resizing `Hash` clears the table, `Threads` takes
  effect from the next `go`.
- Input is read on its own thread. `stop`, `quit` and `isready` are handled during a search, and other commands
  are queued, so nothing is lost when a GUI sends several at once.
- Not yet: pondering, MultiPV.

## Move generation

Bitboards with PEXT (or magic bitboard) slider lookups, make/undo with incremental Zobrist hashing. Perft 6 from
the start position takes under 2 s. It's tested against 6K+ perft positions, and the incremental hash against a
full recompute at every node.

Moves are generated in stages: captures and promotions first, quiet moves only when they're reached, and
`move_is_pseudo_legal` checks the TT move and killers without generating anything.

## Search

Iterative deepening, PVS, and aspiration windows from depth 4 (±25, doubled on each fail, the full window past
±1000).

**Move ordering** (a staged picker): the TT move, winning and equal captures (MVV-LVA plus capture history), queen
promotions, two killers, quiet moves by history, then losing captures (SEE < 0). In check and on the PV all
moves are generated and scored at once.

- **History:** butterfly history (from/to per side), plus continuation history: how good a quiet move has been
  after the moves 1, 2 and 4 plies before. Quiet moves are ordered by the sum. Bonus min(16 × depth², 1600) for
  the move that raised alpha, the same malus for the quiets tried before it, with gravity. The butterfly and
  capture histories are kept between moves and halved before each search; the continuation and correction
  histories are kept for the game. `ucinewgame` clears them all.
- **Correction history:** the static eval is corrected by how wrong it has been in similar positions: tables
  per side for the pawn structure and for each colour's other pieces, at most ±64 cp. Pruning decisions use the
  corrected eval.

**Pruning and reductions:**

| | |
|---|---|
| Reverse futility | depth ≤ 6, eval − 100 × depth ≥ beta (margin 50 smaller when improving) |
| Null move | from depth 2, R = 3 + depth / 3 + min((eval − beta) / 200, 3); verified by a normal search from depth 14 |
| Futility | quiet moves, depth ≤ 8, eval + 50 + 100 × depth ≤ alpha |
| Late move pruning | quiet moves, depth ≤ 6, after 3 + depth² moves (two thirds of that when not improving) |
| SEE pruning | depth ≤ 8: quiets with SEE < −60 × depth, captures with SEE < −20 × depth² |
| History pruning | quiet moves, depth ≤ 4, history < −300 × depth |
| Internal iterative reduction | one ply less from depth 4 when there's no TT move |
| Late move reductions | 0.75 + ln(depth) × ln(moves) / 2.25, from depth 3 after 3 moves; less for good history, more at expected cut nodes and when not improving; killers aren't reduced |

**Extensions:** singular extensions from depth 6 (a TT move that beats every other move by 2 cp × depth in a
half-depth search is extended; one that isn't singular but beat beta is searched a ply shallower) and check
extensions, limited to the first 2 × root depth plies.

**Quiescence:** captures and queen promotions, skipping losing captures (SEE) and with delta pruning (200 cp). It
probes and saves the TT.

**Transposition table:** 16-byte lock-less entries (Hyatt's XOR scheme, so a torn entry reads as a miss), four to
a 64-byte bucket. A save replaces the position's own entry or the bucket's least valuable one (empty, else the
shallowest, entries from earlier searches counting less). Entries hold the best move, score, depth, bound and
static eval. The child's bucket is prefetched after each move. The table is kept between moves and cleared on
`ucinewgame`.

**Lazy SMP:** with `Threads` > 1, helper threads search the same root with their own board and histories,
sharing only the TT. Odd-numbered helpers search one ply deeper. The main thread checks the clock, reports the
total nodes and picks the move.

**Draws:** repetition (game history and search path), the fifty-move rule and insufficient material.

## Syzygy tablebases

With `SyzygyPath` set, tiki probes Syzygy tables through [Fathom](https://github.com/jdart1/Fathom)
(`engine/third_party/fathom`, `engine/core/src/syzygy.c`). Without it nothing changes.

- **At the root:** when the position is in the tables, its moves are ranked with the DTZ tables and only the
  best ranked are searched: every winning move that still wins within the fifty-move rule, or every drawing
  move. The search then chooses among them without probing. If a DTZ table is missing (the 6-piece DTZ tables
  are 81 GB), the WDL tables rank the moves instead and the search keeps probing while winning.
- **In the search:** a position just after a capture or pawn move (the fifty-move counter at 0), without
  castling rights and with no more pieces than the largest tables is looked up in the WDL tables. A win
  scores just below a mate, less the ply, a loss the opposite and a draw 0. The result is stored in the TT
  like a mate score, and shown as ±200 pawns (`cp 20000` less the plies to the tablebase position).
- Wins and losses that the fifty-move rule turns into draws ("cursed" and "blessed") count as draws.

## Time management

From the clock (less `Move Overhead`) and the increment, a base time per move: what's left / 20 + 75% of the
increment (`movestogo n` divides by n instead of 20). Two limits:

- **Hard:** 250% of the base, never more than half of what's left. The search stops wherever it is.
- **Soft:** 60% of the base, checked after each depth: no new depth once it has passed. From depth 5 it's
  scaled by how settled the search is (never past the hard limit):
  - the best move's stability: ×2.5 when it has just changed, down to ×0.75 after four unchanged depths;
  - the best move's share of the root's nodes: ×0.675 when it took them all, up to ×2.0 when it took few.

`movetime` uses all its time; `depth`, `nodes` and `infinite` aren't timed.

## Evaluation

tiki's own network only, see [nnue.md](nnue.md). The accumulators are updated incrementally along the search path,
lazily (only when a position is evaluated). When a king moves, its side is refreshed from a cache of accumulators
per king square ("Finny tables"), applying only the pieces that differ. The accumulator updates and the output
layer use AVX2 when it's built in, with identical results to the plain C version.

## Source layout

| | |
|---|---|
| `engine/core/src` | board, move generation, search (`search.c`), TT, UCI, time management, bench, datagen |
| `engine/core/src/nnue` | network inference, incremental accumulators, file format (`nnue.h`) |
| `engine/core/include/core/tiki.h` | the public interface |
| `engine/app` | the `tiki` executable |
| `engine/test` | Unity tests, run through CTest |
| `engine/benchmark` | Google Benchmark micro-benchmarks |
| `engine/platform/win32` | POSIX headers over Win32, for Windows builds |
| `engine/magic_generator` | generates the magic numbers for magic bitboards |
| `training` | the PyTorch trainer and data tools |
