# Roadmap

As of November 2026. Each change is tested with an SPRT ([testing.md](testing.md#matches)), related changes
together.

## In progress

- **Search tuning:** an SPSA run of the 47 search and time management parameters on OpenBench (8+0.08), then
  its values SPRT'd against `main`.
- **Network:** a network trained on Syzygy-relabelled data (35 months, 2023-12 to 2026-10) with the factoriser,
  SPRT'd against the current one.
- **Syzygy tablebases:** done ([engine.md](engine.md#syzygy-tablebases)). Accepted as no worse: −2.4 ± 15.5
  Elo after 580 games at 8+0.08 with `SyzygyProbeLimit 5`, and in every game that reached the tables the side
  with them scored at least the tablebase result.

## Next

- **Network:**
  - PSQT outputs: a linear piece-square score per output bucket alongside the accumulator;
  - a longer training schedule (100 epochs), checked by the validation loss first;
  - more data: self-play written directly in the binary format, more months, endgame-heavy samples.
- **Search:** a second, shorter SPSA run at a longer time control (about 40+0.4), since the first tunes the margins
  for shallow searches.
- **Strength:** a 300-game gauntlet against Eleanor 4.1 (CCRL Blitz top 100) as the next yardstick, and
  submitting tiki to a rating list.
- **Lazy SMP:** measure the gain at 2, 4 and 8 threads.
- **UCI:** MultiPV and pondering.
