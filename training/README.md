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

   **Or use Leela Chess Zero's self-play** (`lc0.py`). Lc0 publishes its training games at
   https://storage.lczero.org/files/training_data under the Open Database License (ODbL 1.0: a net
   trained on them should credit "Leela Chess Zero training data, ODbL", see the `LICENSE.txt`
   there). Each run (`test80`, ...) adds a `.tar` of games about every hour, 1-2 GB each, so there is
   far more of it than of analysed Lichess games: test80 alone has around 12,000 tars. The script:
   - streams each tar from the server and converts it as it arrives, one output file per tar,
     `--out-dir/lc0-<run>-<YYYYMMDD>-<HHMM>.bin`, so only the output is stored (`--download-dir`
     keeps the tars too). A tar already converted is skipped, so you can run the same command again.
   - reads lc0's training records (V6) and keeps the same quiet positions as datagen (not in check,
     lc0's best move not a capture or promotion). Positions are from the side to move's point of view,
     written as the same position with White to move, which tiki's network evaluates identically.
   - scores each position with lc0's search value for its best move (`--q best`, or `root` /
     `played`), converted from Q to centipawns as lc0 does, and takes the result from the game.

         python3 lc0.py --out-dir /mnt/f/lc0/data --run test80 --dates 2024-04-01:2024-04-30 --per-day 2

   - **Size:** with the defaults a typical tar gives about 1M positions (68 MB), in under a minute or
     two with all cores. `--per-day` (default 2) picks that many tars spread over each day; 0 takes
     all of them (about 24 a day).
   - **Filters:** `--skip-plies` (default 16, lc0 plays its opening moves with some randomness),
     `--sample` (default 0.25), `--max-score`.
   - **Checks:** it stops if more than 1% of a tar's records aren't valid positions, which would mean
     the records aren't V6. Some runs mix in Chess960 games; their later positions are ordinary chess
     positions, and they're kept.
   - **Lc0's judgement:** as with Lichess's Stockfish scores, a net trained on lc0's scores learns
     lc0's evaluation, with the same caveat for rating lists and events.

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
     each month with `lichess.py --max-positions` if needed, or stream it (below).

   **Streaming** (`train_streaming.py`, or `run_training.sh --streaming`) trains on any amount of
   data with fixed memory: months of Lichess data, lc0's, and datagen's together.

       python3 train_streaming.py --data /mnt/f/lichess/data /mnt/f/lc0/data --out nets --epochs 20

   - `--data` takes files or directories (their `.bin` and `.txt` files; `.part` files of a
     conversion still running are left out).
   - Each pass reads chunks of consecutive positions (about 17 MB) in a random order across all the
     files, and shuffles them in a buffer of `--buffer` positions (default 8M, about 0.5 GB), so a batch
     mixes many games, months and sources. The next buffer is read in the background while the GPU
     trains, so about two buffers are in memory. Reading from a Windows drive in WSL runs at over 1M
     positions a second, faster than training.
   - `--epoch-positions N` makes an epoch N positions rather than a whole pass, for data too large to
     pass over often: each epoch still ends with validation, a checkpoint, an exported net and the
     `--lr-drop` schedule. `--validation-max` (default 2M) caps the positions validated each epoch.
   - Validation is whole games held out of each file, chosen from the file's name and `--seed`, so
     adding files later doesn't change what the others hold out.
   - Checkpoints are the same as `train.py`'s, so either can `--resume` the other's.

   After each epoch this writes `nets/tiki-<epoch>.pt` (a checkpoint, for `--resume`) and
   `nets/tiki-<epoch>.nnue` (the engine format). Progress bars show the loss, positions per second
   and time remaining, and progress is also logged to `<out>/train.log` (every 10% of an epoch),
   so `tail -f` it to check on a run from another terminal. Options:
   - `--wdl` sets the weight of the game result against the search score (default 0.3).
   - `--lr` and `--lr-drop` set the learning rate schedule.
   - `--validation` sets the fraction held out for the validation loss (default 0.02). It's held out
     in blocks of 4,096 consecutive positions, and the data files keep each game's positions together,
     so validation is whole games the network never trains on (positions from a game it trained on
     would make the validation loss look better than it is).
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
- `test_stream.py`: the streaming trainer's data:
  - training and validation cover every record of every file once, and validation stays the same
    when files are added;
  - a pass yields every training record once, mixed across files, and the next pass reorders them;
  - epochs of a fixed number of batches, and the validation limit;
  - a small `train_streaming.py` run that trains and exports.
- `test_lc0.py`: builds lc0 records as lc0 encodes them and checks:
  - the decoded positions have the real position's features, for either side to move;
  - the policy move table against lc0's published one;
  - the quiet-position filter (captures, en passant, promotions, check) and the other filters;
  - rejecting records that aren't positions or V6.

### Next steps

- **Compare nets in games** (SPRT), a new net against the current one.
- **Data:**
  - multi-threaded datagen, writing the binary format directly;
  - a node limit instead of a depth limit.
- **Bigger nets:** a larger L1 and output buckets (by material), once there is enough data.
