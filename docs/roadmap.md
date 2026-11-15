# Roadmap

As of 15 November 2026. Each change is tested on its own with an SPRT ([testing.md](testing.md#matches)), the
parts of one mechanism together.

## Done recently

- **Search tuning:** an SPSA run of the search parameters on OpenBench (8+0.08, about 19,500 games). Its values
  came out level against `main` (-0.7 ± 16.4 after 520 games) and weren't taken; they're kept as a patch, the
  starting point of a later tune.
- **Network:** trained on Syzygy-relabelled, rescaled data with the factoriser (+13.1 ± 6.8 Elo), then pairwise
  inputs and a hidden layer (512 -> 16 -> 1) in 32 layer stacks by piece count × queens (+18.3 ± 8.3, despite
  about 6% lower search speed), then a finer output-layer scale (format version 6: +4.5 ± 9.1 after 1,780
  games, kept), then 46 months of data (2.34B positions, 100 epochs: `tiki-2026-11-14.nnue`,
  [nnue.md](nnue.md#the-current-network)). PSQT was tried and removed (worse validation, no gain in games).
  Loss power 2.5 came out roughly level in short SPRTs; judged later on whether it avoids odd losses.
- **Repetitions and en passant:** Stockfish's repetition rule (a draw on a repetition inside the search, or a
  third occurrence), and the en passant square set and hashed only when a pawn can capture there (before, a
  threefold whose first occurrence followed a double push was missed).
- **Search:** ProbCut; a faster search with the same results (the TT entry prefetched when a move is picked,
  and pruned quiet moves no longer made first); Lazy SMP with a shared correction history and a vote between
  threads for the move.
- **Strength:** gauntlets at 8+0.08 with `tiki-2026-11-14.nnue`: +24 ± 25 against Eleanor 4.1 and +108 ± 27
  against Leorik 3.2.1 (300 games each).
- **Fail-soft and Stockfish's LMR pieces:** fail-soft (scores outside the window returned as found,
  futility-pruned moves counted at their futility value as Stockfish does), +13.5 ± 8.8 Elo in 1,902 games of a no-worse SPRT [-3, 1], stopped
  with the whole range above zero; meant mainly as the base for later search changes. Stockfish's deeper/shallower
  LMR re-search on top of it lost about 30 Elo and was dropped for now: Stockfish reduces expected cut nodes 2-4
  plies more than tiki, so its extra ply mostly buys back depth tiki never took. Tested since, each stopped early
  and undecided: a TT-PV flag (a spare bit of the TT entry's depth field) with Stockfish's smaller reductions at
  TT-PV nodes (0.0 ± 21.3, 342 games), the same with a second ply at cut nodes (-1.5 ± 24.3, 240 games), and
  Stockfish's damping of fail-soft scores towards beta (-26.1 ± 31.6, 160 games). By the depths reached in those
  games the TT-PV term costs about 0.4 ply at equal time and still came out level, so it buys accuracy near the
  PV that nothing pays for (the second cut-node ply bought back 0.02 ply); damping cost no depth (0.05 ply).

## In progress

- **Network:** PP_3Wide (Pawn_SN) and a second hidden layer.
- **Time on a falling score** (Stockfish's "falling eval", which tiki and Eleanor lack): the soft limit grows by
  `TM_FALLING_PREV` % per 10 cp the score is below the previous search's and `TM_FALLING_ITER` % per 10 cp below
  the score four depths before, up to `TM_FALLING_MAX` (10, 5, 200 to start, tunable). Prompted by the loss
  analysis (`training/analyse_games.py`) of the net95 gauntlets: about 45% of the turning points of the openings
  tiki lost against Leorik and Eleanor were "unreproduced", where a fresh search at the game's depth finds a
  saving move. Searched alone, the bad move scores 60-210 cp worse than the good one, but in the game's search
  the refutation was reduced or pruned (weakening any of six pruning techniques finds it, no one of them is the
  cause). A blunder suite (65 such positions replayed warm at the games' times, 10 runs each: 42% blunders
  for the gauntlet build) can only show the gain, not the time taken from later moves, so it's tested with a
  no-worse SPRT [-3, 1] against `main`; the three percentages are left to a later SPSA.

## Next

- **Network:**
  - reorder the accumulator after training so zero inputs share groups of 4, then skip them (Stockfish's sparse
    first layer; without reordering over 80% of groups have a non-zero input, and skipping was slower);
  - `--loss-power 2.5` (Stockfish's exponent), on its own run;
  - fine-tuning on Leela's positions (the Stockfish binpacks, scored by Leela), from the Lichess network;
  - more data: self-play written directly in the binary format, endgame-heavy samples;
  - endgame training data that teaches progress, not just the result: the network knows K+R v K, K+Q v K and
    K+B+N v K are won but not how to win them. Over 300 random positions each (defender to move), its static
    eval falls as the defending king nears a corner (K+Q v K: 754 with the king in the centre, 542 in a corner;
    K+B+N v K: 415 and 344; K+R v K: 651 and 617), probably from human games' stalemate tricks, and without
    tablebases tiki drew 4 tablebase-won endings in the net95 gauntlets against Leorik and Eleanor (3 by the
    fifty-move rule). Positions from these endings scored by distance to mate or to a zeroing
    move (Syzygy DTZ), oversampled so the network sees enough of them.
- **Mop-up evaluation: written and parked, not to be committed.** The fix for bare-king endings is endgame
  training data (Network, above); with Syzygy DTZ tables (as on CCRL) tiki's root ranking already wins them. The
  stopgap is kept as a patch, `tiki-analysis\mop-up.patch` (applies to 27ef41b): a bare king against queen,
  rook, bishops on both colours or bishop and knight, with no pawns, scored by `mop_up.h` instead of the network
  (a fixed win plus pushing the defending king to the edge, with B+N to a corner of the bishop's colour, and
  the kings together), and the correction history skipped there (corrections learnt by the defending king's
  square flattened the push). Bench unchanged, no measurable speed cost. Against Stockfish's defence K+Q and
  K+B+N mate at 0.1-0.3 s a move; K+R is still unreliable at 0.1 s (the search's score sits at mop-up's
  maximum, so its moves tie; a rook "box" term would be needed).
- **Razoring** (tiki has none; Stockfish, Eleanor and Leorik all do): at a non-PV, expected fail-low node of
  depth 1-3 whose corrected eval is far below alpha (a tunable margin per ply, about 200 cp to start), return
  the qsearch score instead of searching the node. Futility pruning already skips the quiet moves there, but
  the node still generates moves and searches every capture a ply deeper. As its own SPRT.
- **Aspiration windows** (`aspiration_search`), each as its own SPRT:
  - widen from the score the search returned: on a fail the failing bound moves by delta from where it was,
    though the fail-soft score now says how far outside the window the value lies;
  - on a fail low, bring beta in to (alpha + beta) / 2, as Stockfish does;
  - grow delta by about 1.5× per fail rather than doubling it.
- **Stockfish's LMR pieces again:**
  - damping together with the deeper/shallower re-search, as one SPRT: Stockfish's re-search margins compare
    damped scores, and tiki tried the re-search on undamped ones (a reduced search's fail high then looks better
    than it is and asks for the extra ply too often) and damping alone, never both;
  - fractional reductions (units of 1/1024 ply), so the terms the SPSA puts between whole plies (cut node 1.27,
    not improving 0.35) can be set, and a TT-PV term of a third to half a ply tuned rather than tested at a
    whole one;
  - before the TT-PV term returns, a statistics build's count of the reduced searches at TT-PV nodes outside the
    PV, by depth: whether the flag marks more of the tree than intended;
  - what would pay for less reduction near the PV: reducing more move types (good captures, killers, earlier
    moves). The SPSA wants slightly less reduction of late quiet moves, not more.
- **Search:** another SPSA run from the first one's values, after the current changes, and a shorter one at a
  longer time control (about 40+0.4), since 8+0.08 tunes the margins for shallow searches.
- **Strength:** submitting tiki to a rating list.
- **Lazy SMP:** measure the gain at 2, 4 and 8 threads, and SPRT the thread vote at 4 threads.
- **UCI:** MultiPV and pondering.
