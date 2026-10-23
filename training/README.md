## Training tiki's NNUE

A small PyTorch trainer for tiki's own network. The engine side is `engine/core/src/nnue/nnue.h`
(features, quantisation, file format) and `engine/core/src/datagen.h` (training data). Both sides
implement the features and the integer evaluation, and the tests check that they agree.

### Network

    HalfKAv2_hm (22528) -> 256 per side -> SCReLU -> concat (512) -> 1

- **Features (HalfKAv2_hm).** For each side ("perspective"), every piece is a feature indexed by
  (king bucket, piece kind, square). Squares are oriented so the perspective plays up the board,
  and mirrored left-right when its king is on files e-h, so there are 32 king buckets. The kinds
  are own/their pawn, knight, bishop, rook and queen (10), plus one shared kind for both kings
  (the "A"). So 32 × 11 × 64 = 22528 inputs, and at most 32 are active.
- **Accumulator.** 256 values per perspective: the feature transformer's bias plus the sum of
  the active features' rows. The side to move's half comes first in the output layer.
- **Quantisation.** Accumulator × 255 (QA) as int16, output weights × 64 (QB) as int16, output
  bias × QA × QB as int32. Eval in centipawns = network output × 400 (SCALE).

### Pipeline

1. **Build the engine** (see the top-level README).

2. **Generate data** with self-play. The arguments are file, games, depth and seed. Run several
   processes with different seeds and the same or different files:

       build/engine/app/tiki_app datagen data/d8-s1.txt 10000 8 1

   Each line is `fen | score | result`, with the score in centipawns and the result
   1.0 / 0.5 / 0.0, both from white's side. Only quiet positions are kept (not in check, best move
   not a capture or promotion). At depth 8 expect very roughly 100 positions per second per
   process. A first useful net needs millions of positions; more data matters more than more
   epochs.

3. **Train:**

       pip install -r training/requirements.txt
       cd training
       python3 train.py --data ../data/*.txt --out nets --epochs 20

   After each epoch this writes `nets/tiki-<epoch>.pt` (a checkpoint, for `--resume`) and
   `nets/tiki-<epoch>.nnue` (the engine format). Options:
   - `--wdl` sets the weight of the game result against the search score (default 0.3).
   - `--lr` and `--lr-drop` set the learning rate schedule.
   - `--device` defaults to CUDA when it's available.

4. **Check a network in the engine:**

       build/engine/app/tiki_app nnue-eval nets/tiki-20.nnue "<fen>"

   This prints the evaluation from the side to move's point of view. The engine still searches
   with the hand-crafted eval; using the net in search comes next (see Next steps).

### Tests

    cd training
    TIKI_APP=../build/engine/app/tiki_app python3 -m unittest discover -s tests

- `test_features.py`: known indices (the same values the C tests use), range, uniqueness, and
  colour-flip and mirror symmetry.
- `test_model.py`:
  - colour symmetry of the float model;
  - converting white-relative scores to the side to move;
  - the file round trip;
  - the integer eval matching the float model on the quantised weights;
  - a small training run reducing the loss.
- `test_cross_check.py`: exports a network and checks that `tiki_app nnue-eval` gives exactly
  `quantised_eval()`. It's skipped unless `TIKI_APP` is set.

### Next steps

- **Use the net in search:**
  - keep the accumulators in make/undo, updated incrementally, with a full refresh when the
    king's bucket or mirror changes;
  - add a UCI `EvalFile` option;
  - add SIMD (AVX2) for the accumulator and the output layer;
  - then compare with the HCE in games (SPRT).
- **Data:**
  - a binary format and multi-threaded datagen once the text format becomes the bottleneck;
  - a node limit instead of a depth limit.
- **Bigger nets:** a larger L1 and output buckets (by material), once there is enough data.
