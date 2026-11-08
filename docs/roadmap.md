# Roadmap

As of November 2026. Each change is tested with an SPRT ([testing.md](testing.md#matches)), related changes
together.

## In progress

- **Search tuning:** an SPSA run of the 47 search and time management parameters on OpenBench (8+0.08), then
  its values SPRT'd against `main`.
- **Network:** done: trained on Syzygy-relabelled, rescaled data (38 months, 2023-09 to 2026-10) with the
  factoriser, +13.1 ± 6.8 Elo over the previous network ([nnue.md](nnue.md#the-current-network)). Next is the
  same recipe with PSQT.

## Next

- **Network:**
  - PSQT: implemented (`--psqt`, format version 3, tested incremental and against the trainer), needs a full
    training run and an SPRT;
  - output buckets by material (non-pawn material, or queens on/off × piece count) instead of piece count;
  - a longer training schedule (100 epochs), checked by the validation loss first;
  - more data: self-play written directly in the binary format, more months, endgame-heavy samples.
- **Search:** a second, shorter SPSA run at a longer time control (about 40+0.4), since the first tunes the margins
  for shallow searches.
- **Strength:** a 300-game gauntlet against Eleanor 4.1 (CCRL Blitz top 100) as the next yardstick, and
  submitting tiki to a rating list.
- **Lazy SMP:** measure the gain at 2, 4 and 8 threads.
- **UCI:** MultiPV and pondering.
