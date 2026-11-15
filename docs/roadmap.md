# Roadmap

As of November 2026. Each change is tested with an SPRT ([testing.md](testing.md#matches)), related changes
together.

## In progress

- **Search tuning:** an SPSA run of the 47 search and time management parameters on OpenBench (8+0.08), then
  its values SPRT'd against `main`.
- **Network:** done: trained on Syzygy-relabelled, rescaled data (38 months, 2023-09 to 2026-10) with the
  factoriser, +13.1 ± 6.8 Elo over the previous network ([nnue.md](nnue.md#the-current-network)). PSQT was tried
  on the same recipe and removed (worse validation, no gain in games). Then pairwise inputs and a hidden layer
  (512 -> 16 -> 1) in 32 layer stacks by piece count × queens (format version 5): +18.3 ± 8.3 Elo over the
  previous network on the same data, despite about 6% lower search speed ([nnue.md](nnue.md#the-current-network)).
  From here, one change per test: loss power 2.5, more data, a second hidden layer. Loss power 2.5 came out
  roughly level in short SPRTs (epoch 78 and the average of epochs 77-80); judged later on whether it avoids odd
  losses rather than on Elo.
- **Search:** done: fail-soft (scores outside the window returned as found, futility-pruned moves counted at
  their futility value as Stockfish does), +13.5 ± 8.8 Elo in 1,902 games of a no-worse SPRT [-3, 1], stopped
  with the whole range above zero; meant mainly as the base for later search changes. Stockfish's deeper/shallower
  LMR re-search on top of it lost about 30 Elo and was dropped for now: Stockfish reduces expected cut nodes 2-4
  plies more than tiki, so its extra ply mostly buys back depth tiki never took. Tested since, each stopped early
  and undecided: a TT-PV flag (a spare bit of the TT entry's depth field) with Stockfish's smaller reductions at
  TT-PV nodes (0.0 ± 21.3, 342 games), the same with a second ply at cut nodes (-1.5 ± 24.3, 240 games), and
  Stockfish's damping of fail-soft scores towards beta (-26.1 ± 31.6, 160 games). By the depths reached in those
  games the TT-PV term costs about 0.4 ply at equal time and still came out level, so it buys accuracy near the
  PV that nothing pays for (the second cut-node ply bought back 0.02 ply); damping cost no depth (0.05 ply).

## Next

- **Network:**
  - a finer output-layer scale: `out_weights` are int16 but quantised at QW = 64 (largest |w| × 64 is 88), and
    their rounding is the largest quantisation error (4.1 cp sd of 5.3 between the float and engine evals of
    the current network; the `>> 9` rounding down is 1.4 cp sd with no bias). A scale of about 2048 costs
    nothing at runtime; needs a format version bump, the exporter, a re-export and an SPRT;
  - reorder the accumulator after training so zero inputs share groups of 4, then skip them (Stockfish's sparse
    first layer; without reordering over 80% of groups have a non-zero input, and skipping was slower);
  - AVX-512 VNNI / AVX-VNNI for the hidden layer (one instruction for maddubs + madd + add);
  - a longer training schedule (100 epochs), checked by the validation loss first;
  - `--loss-power 2.5` (Stockfish's exponent), on its own run;
  - fine-tuning on Leela's positions (the Stockfish binpacks, scored by Leela), from the Lichess network;
  - more data: self-play written directly in the binary format, more months, endgame-heavy samples;
  - endgame training data that teaches progress, not just the result: the network knows K+R v K, K+Q v K and
    K+B+N v K are won but not how to win them. Over 300 random positions each (defender to move), its static
    eval falls as the defending king nears a corner (K+Q v K: 754 with the king in the centre, 542 in a corner;
    K+B+N v K: 415 and 344; K+R v K: 651 and 617), probably from human games' stalemate tricks, and without
    tablebases tiki drew 4 tablebase-won endings in the net95 gauntlets against Leorik and Eleanor (3 by the
    fifty-move rule). Positions from these endings scored by distance to mate or to a zeroing
    move (Syzygy DTZ), oversampled so the network sees enough of them.
- **En passant in the position hash, first after the SPRT of the tuned values:** the en passant square is set and
  hashed after every double pawn push, even when no pawn can capture there, so the position after a double push
  never matches its later repeats and a threefold whose first occurrence followed one is missed (fastchess:
  "PV continues after threefold repetition"). The old repetition rule hid it. Set and hash the square only when
  an enemy pawn attacks it, in `make_move` and the FEN parser; the bench changes (such positions now share
  transposition table entries).
- **Repetitions:** Stockfish's rule (a draw on a repetition inside the search, or a third occurrence; one earlier
  occurrence in the game no longer counts). Written, in the working tree; after the SPSA: tests, unchanged
  bench, a 1,000-game match against `main` for gross bugs and a look at the repetition games.
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
  the node still generates moves and searches every capture a ply deeper. After the SPSA, as its own SPRT.
- **Aspiration windows** (`aspiration_search`), each as its own SPRT after the SPSA has set `ASPIRATION_WINDOW`:
  - widen from the score the search returned: on a fail the failing bound moves by delta from where it was,
    though the fail-soft score now says how far outside the window the value lies;
  - on a fail low, bring beta in to (alpha + beta) / 2, as Stockfish does;
  - grow delta by about 1.5× per fail rather than doubling it.
- **Stockfish's LMR pieces again**, after the SPSA:
  - damping together with the deeper/shallower re-search, as one SPRT: Stockfish's re-search margins compare
    damped scores, and tiki tried the re-search on undamped ones (a reduced search's fail high then looks better
    than it is and asks for the extra ply too often) and damping alone, never both;
  - fractional reductions (units of 1/1024 ply), so the terms the SPSA puts between whole plies (cut node 1.27,
    not improving 0.35) can be set, and a TT-PV term of a third to half a ply tuned rather than tested at a
    whole one;
  - before the TT-PV term returns, a statistics build's count of the reduced searches at TT-PV nodes outside the
    PV, by depth: whether the flag marks more of the tree than intended;
  - what would pay for less reduction near the PV: reducing more move types (good captures, killers, earlier
    moves), then ProbCut. The SPSA wants slightly less reduction of late quiet moves, not more.
- **Search:** a second, shorter SPSA run at a longer time control (about 40+0.4), since the first tunes the margins
  for shallow searches.
- **Strength:** a 300-game gauntlet against Eleanor 4.1 (CCRL Blitz top 100) as the next yardstick, and
  submitting tiki to a rating list.
- **Lazy SMP:** measure the gain at 2, 4 and 8 threads.
- **UCI:** MultiPV and pondering.
