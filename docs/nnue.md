# The network

```text
HalfKAv2_hm (22528) -> 512 per side -> SCReLU -> concat (1024) -> 1, one of 8 output layers by piece count
```

- **Inputs (HalfKAv2_hm):** for each side, every piece is a feature indexed by (king bucket, piece kind, square),
  with the board mirrored so the king is on files a-d: 32 king buckets × 11 kinds × 64 squares. The kinds are
  own and enemy pawn, knight, bishop, rook and queen, plus one for both kings.
- **Accumulator:** 512 values per side, updated incrementally in the engine.
- **Output:** 8 output layers, chosen by the number of pieces ((pieces − 2) / 4), so the opening, middlegame and
  endgame each weigh the accumulator their own way.
- **Quantisation:** accumulator × 255 as int16, output weights × 64 within the int8 range (so AVX2 `madd` can be
  used), eval = output × 400 cp.

The engine side is `engine/core/src/nnue/` (`nnue.h` defines the features, quantisation and file format). The
trainer's tests check that both sides compute the same features and integer evaluation.

## The current network

`models/tiki-2026-11-10.nnue`: 1.71B positions from 33 months of Lichess games (2024-01 to 2026-09) between
players rated 2000+, labelled with Lichess's Stockfish evaluations (70%) and the game results (30%). 65 epochs of
100M positions with a cosine learning rate schedule. It beat the previous network (1.1B positions, 2025-01 to
2026-09) by +24 ± 12 Elo.

## Training

[`training/README.md`](../training/README.md) has the details. In short:

```bash
python3 training/lichess_download.py --download-dir <dir> --months 2024-01:2026-09     # Lichess monthly dumps
training/lichess_convert/lichess_convert --min-elo 2000 <month>.pgn.zst <month>.bin
training/syzygy_relabel/syzygy_relabel --tb <tables> <data dir> <relabelled dir>    # optional
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
