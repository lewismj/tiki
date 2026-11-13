# The network

```text
HalfKAv2_hm (22528) -> 512 per side -> pairwise -> concat (512) -> 16 -> CReLU -> 1
                                                   \__ one of 32 layer stacks: piece count x queens __/
```

Network format version 6. Version 5 (`tiki-2026-11-08.nnue`) is the same network with the output layer at a
coarser scale; the trainer converts it exactly (`write_network(read_network(...))`). The networks in `models/` up
to `tiki-2026-11-07.nnue` are version 2 (an output layer straight from the accumulator, 8 buckets by piece count)
and need an engine from before the hidden layer; builds that embed a network pass over every other version
(CMake reads each file's version).

- **Inputs (HalfKAv2_hm):** for each side, every piece is a feature indexed by (king bucket, piece kind, square),
  with the board mirrored so the king is on files a-d: 32 king buckets × 11 kinds × 64 squares. The kinds are
  own and enemy pawn, knight, bishop, rook and queen, plus one for both kings.
- **Accumulator:** 512 values per side, updated incrementally in the engine.
- **Pairwise:** each side's first 256 values times its second 256, each clamped to 0..1 (as Stockfish), 512 inputs
  to the hidden layer. Trained as well as 1024 SCReLU inputs on a short run, at half the cost.
- **Layer stacks:** 32 small networks (512 -> 16 -> 1), chosen as in Stockfish's SFNNv18: the number of pieces
  ((pieces − 2) / 4) × 4, plus 2 if the side to move has a queen and 1 if the other side has. Only the position's
  own stack is evaluated. The network after the accumulator takes about 116 ns against 52 ns before, about 6% of
  search speed (AVX2).
- **Quantisation:** accumulator × 255 as int16; pairwise products 0..127 as uint8, weights × 64 as int8 (`maddubs`, or
  one VNNI `vpdpbusd` where the CPU has it), CReLU at 8128; output weights × 2048 as int16 (× 64 in version 5, whose rounding was the largest
  quantisation error, about 4 cp sd); eval = output × 400 cp. All integer, so every build
  (any CPU level) gives the same evaluation and bench.

The engine side is `engine/core/src/nnue/` (`nnue.h` defines the features, quantisation and file format). The
trainer's tests check that both sides compute the same features and integer evaluation.

## The current network

`models/tiki-2026-11-08.nnue`, the first of this architecture (format version 5): 1.95B positions from 38 months
of Lichess games (2023-09 to 2026-10) between players rated 2000+, labelled with Lichess's Stockfish evaluations
(70%) and the game results (30%). The results of positions with 6 pieces or fewer were corrected with the Syzygy
tables, and each month's scores rescaled to the same scale (`s` 335). Trained with the factoriser (for the
accumulator and the layer stacks), 80 epochs of 100M positions with a cosine learning rate schedule and loss power
2; epoch 78 is the network (validation 0.021003, against 0.021329 for the best epoch of the previous
architecture on the same data).

It beat the previous network (`tiki-2026-11-07.nnue`: the same data, the version 2 architecture, 65 epochs) by
**+18.3 ± 8.3 Elo** (SPRT [0, 5] passed, 2,722 games at 8+0.08), although it searches about 6% fewer nodes per
second (that SPRT's builds were AVX2, without VNNI). Before it, `tiki-2026-11-07.nnue` had beaten
`tiki-2026-11-06.nnue` (1.71B positions, 2024-01 to 2026-09, no relabelling, rescaling or factoriser) by
+13.1 ± 6.8, which had beaten the one before it (1.1B positions, 2025-01 to 2026-09) by +24 ± 12.

## Training

[`training/README.md`](../training/README.md) has the details. In short:

```bash
python3 training/lichess_download.py --download-dir <dir> --months 2024-01:2026-09     # Lichess monthly dumps
training/lichess_convert/lichess_convert --min-elo 2000 <month>.pgn.zst <month>.bin
training/syzygy_relabel/syzygy_relabel --tb <tables> <data dir> <relabelled dir>
python3 training/rescale.py --scale-to 335 <relabelled dir> <rescaled dir>    # see training/README.md, "Adding a Lichess month"
python3 training/train_streaming.py --data <dir> --out <nets> --epochs 65 --epoch-positions 100000000 \
        --lr-schedule cosine --wdl 0.3 [--factoriser]
build/engine/app/tiki nnue-eval <nets>/tiki-65.nnue "<fen>"
```

- **Data sources:** Lichess's analysed games (Stockfish evaluations), tiki's self-play (`tiki datagen`), and Leela
  Chess Zero's self-play (`training/lc0.py`, ODbL). A Leela-only network lost to the Lichess one, so Lichess is
  the default.
- **Syzygy relabelling:** replaces the game result of positions with 6 or fewer pieces with the tablebase result;
  about a third of them were wrong.
- **Factoriser:** trains weights shared by all king buckets as well, added into each bucket on export (the network
  file is unchanged).
- **Hidden layer:** each bucket's hidden layer is trained as its own plus one shared by every bucket, folded together
  on export, so rare buckets (few pieces with queens, many pieces without) start from what the common ones learnt.
- **PSQT (removed):** a value per feature and output bucket added to the output, as Stockfish had until SFNNv17.
  Trained on the same recipe it was behind at every epoch (validation 0.021374 vs 0.021330) and -6 ± 20 Elo after
  340 games, so it was taken out (Stockfish also removed it, as worth nothing).
