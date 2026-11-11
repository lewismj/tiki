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
  plies more than tiki, so its extra ply mostly buys back depth tiki never took. Next, one change per test: a
  TT-PV flag (a spare bit of the TT entry's depth field) with Stockfish's smaller reductions at TT-PV nodes,
  harder cut-node reductions, reducing more move types; then ProbCut and deeper/shallower again.

## Next

- **Network:**
  - reorder the accumulator after training so zero inputs share groups of 4, then skip them (Stockfish's sparse
    first layer; without reordering over 80% of groups have a non-zero input, and skipping was slower);
  - AVX-512 VNNI / AVX-VNNI for the hidden layer (one instruction for maddubs + madd + add);
  - a longer training schedule (100 epochs), checked by the validation loss first;
  - `--loss-power 2.5` (Stockfish's exponent), on its own run;
  - fine-tuning on Leela's positions (the Stockfish binpacks, scored by Leela), from the Lichess network;
  - more data: self-play written directly in the binary format, more months, endgame-heavy samples.
- **Search:** a second, shorter SPSA run at a longer time control (about 40+0.4), since the first tunes the margins
  for shallow searches.
- **Strength:** a 300-game gauntlet against Eleanor 4.1 (CCRL Blitz top 100) as the next yardstick, and
  submitting tiki to a rating list.
- **Lazy SMP:** measure the gain at 2, 4 and 8 threads.
- **UCI:** MultiPV and pondering.
