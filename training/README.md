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

   **Or use Lichess games** (`lichess.py`). The monthly dumps from https://database.lichess.org (CC0)
   include the games Lichess analysed, which have a Stockfish eval after every move, and their
   results. The script:
   - downloads each month to `--download-dir`, resuming if a download is interrupted;
   - reads the `.pgn.zst` dump without unpacking it, and skips the games without evals unparsed;
   - keeps the same positions as datagen (the same quiet-position filter), written straight to the
     trainer's binary format as `--out-dir/lichess-YYYY-MM.bin`. With `--text` it writes
     `lichess-YYYY-MM.txt` in datagen's `fen | score | result` format instead, which is easier to
     inspect.

       python3 lichess.py --download-dir /mnt/f/lichess --out-dir /mnt/f/lichess/data \
                          --months 2024-01:2024-03 --max-positions 20000000

   - **Size:** a recent month is 30 GB or more to download, and scanning it takes a while.
     `--delete-dumps` removes each dump once it's converted.
   - **Re-running:** months already converted are skipped, so you can run the same command again.
   - **Filters:** `--skip-plies` (default 16) drops the opening, `--sample` (default 0.5) the
     fraction of positions kept, `--min-elo` the minimum rating of both players, and
     `--max-score` the largest score kept.
   - **Stockfish's judgement:** the scores are Stockfish's, so a net trained on them learns
     Stockfish's evaluation. That's the quick way to a strong first net, but it isn't tiki's own.
     Some rating lists and events care where a net's data came from, so check their rules if you
     plan to enter. Retraining on tiki's self-play once the net is in search makes later nets
     tiki's own.

3. **Train:**

       pip install -r training/requirements.txt
       cd training
       python3 train.py --data ../data/*.txt --out nets --epochs 20

   Or use `run_training.sh`, which runs the tests first and then trains on every `.txt` and `.bin`
   file in one or more data directories, passing any other options to `train.py`:

       training/run_training.sh --data-dir /mnt/f/lichess/data --data-dir data --out /mnt/f/tiki/nets --epochs 20

   - **Binary data:** training reads `.bin` files (`tiki_nnue/binary.py`), 68 bytes per position:
     the pieces, the score, the result and the side to move. A `.txt` file is converted to a
     `.bin` beside it on first use (`--workers` sets the number of processes), and again if the
     `.txt` changes. The features of each batch are computed on the GPU, so training never parses
     FENs.
   - **Memory:** the data is loaded into memory, so 50M positions needs about 3.4 GB of RAM. Cap
     each month with `lichess.py --max-positions` if needed.

   After each epoch this writes `nets/tiki-<epoch>.pt` (a checkpoint, for `--resume`) and
   `nets/tiki-<epoch>.nnue` (the engine format). Progress bars show the loss, positions per second
   and time remaining, and progress is also logged to `<out>/train.log` (every 10% of an epoch),
   so `tail -f` it to check on a run from another terminal. Options:
   - `--wdl` sets the weight of the game result against the search score (default 0.3).
   - `--lr` and `--lr-drop` set the learning rate schedule.
   - `--device` defaults to CUDA when it's available.

4. **Check a network in the engine:**

       build/engine/app/tiki_app nnue-eval nets/tiki-20.nnue "<fen>"

   This prints the evaluation from the side to move's point of view.

5. **Search with it:** the engine evaluates with the network named by `TIKI_EVALFILE` (or the
   `EvalFile` UCI option), and won't start without one:

       TIKI_EVALFILE=nets/tiki-20.nnue build/engine/app/tiki_app bench

   `tiki_app datagen` searches with it too, so self-play data then comes from tiki's own net.

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
- `test_data.py`:
  - the batch features computed from binary records match `parse_line` and `collate`, including
    on positions from random games (with python-chess);
  - converting text to binary (blank lines, CRLF, a missing final newline, any number of
    workers) and loading the result;
  - rejecting files that aren't binary data or are truncated.
- `test_lichess.py`: converts a small `.pgn.zst` built in the test, and checks:
  - only analysed games are parsed;
  - the quiet-position, mate-score and opening filters;
  - the Elo, score and sample filters;
  - that the output reads back in the trainer, and that the binary output is the text output
    encoded.

  It's skipped if `python-chess` or `zstandard` isn't installed.

### Next steps

- **Compare nets in games** (SPRT), a new net against the current one.
- **Data:**
  - multi-threaded datagen, writing the binary format directly;
  - a node limit instead of a depth limit.
- **Bigger nets:** a larger L1 and output buckets (by material), once there is enough data.
