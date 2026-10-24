## Training tiki's NNUE

A small PyTorch trainer for tiki's own network. The engine side is `engine/core/src/nnue/nnue.h`
(features, quantisation, file format) and `engine/core/src/datagen.h` (training data). Both sides
implement the features and the integer evaluation, and the tests check that they agree.

### Network

```text
HalfKAv2_hm (22528) -> 256 per side -> SCReLU -> concat (512) -> 1
```

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

| Step | Script | Output |
|---|---|---|
| 1. [Build the engine](#1-build-the-engine) | see the top-level README | `tiki_app` |
| 2. [Get training data](#2-get-training-data) | `tiki_app datagen`, `lichess.py`, `lc0.py` | `.txt` / `.bin` data files |
| 3. [Train](#3-train) | `train.py`, `train_streaming.py`, `run_training.sh` | `tiki-<epoch>.nnue` per epoch |
| 4. [Check a network](#4-check-a-network) | `tiki_app nnue-eval`, `tiki_app bench` | |
| 5. [Test it in games](#5-test-it-in-games) | `scripts/match.bat`, `scripts/match.sh` | Elo against the current net |
| 6. [Promote it](#6-promote-it) | copy into `models/` | the engine's default network |

### 1. Build the engine

See the top-level README.

### 2. Get training data

All three sources write the same formats: datagen's text (`fen | score | result`) or the trainer's
binary format (68 bytes per position), and the trainer reads either. Only quiet positions are kept
(not in check, best move not a capture or promotion). A first useful net needs millions of
positions; more data matters more than more epochs.

#### Self-play (`tiki_app datagen`)

The arguments are file, games, depth and seed. Run several processes with different seeds and the
same or different files:

```bash
build/engine/app/tiki_app datagen data/d8-s1.txt 10000 8 1
```

Each line is `fen | score | result`, with the score in centipawns and the result 1.0 / 0.5 / 0.0,
both from white's side. At depth 8 expect very roughly 100 positions per second per process.

#### Lichess games (`lichess.py`)

The monthly dumps from https://database.lichess.org (CC0) include the games Lichess analysed, which
have a Stockfish eval after every move, and their results. The script downloads each month (resuming
an interrupted download), reads the `.pgn.zst` dump without unpacking it, skips the games without
evals unparsed, and writes `--out-dir/lichess-YYYY-MM.bin`.

```bash
python3 lichess.py --download-dir /mnt/f/lichess --out-dir /mnt/f/lichess/data \
                   --months 2024-01:2024-03 --max-positions 20000000
```

| Option | Default | |
|---|---|---|
| `--download-dir` | required | where the dumps go, 30 GB or more a month |
| `--out-dir` | required | where the converted data goes |
| `--months` | required | `YYYY-MM`, or ranges `YYYY-MM:YYYY-MM` |
| `--skip-plies` | 16 | opening plies dropped (book moves) |
| `--sample` | 0.5 | fraction of the remaining positions kept |
| `--min-elo` | 0 | minimum rating of both players |
| `--max-score` | 3000 | largest score kept, in centipawns |
| `--max-positions` | 0 (all) | stop each month after this many |
| `--text` | | write datagen's `.txt` format instead, easier to inspect |
| `--delete-dumps` | | delete each dump once it's converted |
| `--workers` | cores - 1 | conversion processes |
| `--seed` | 1 | sampling seed |

- **Re-running:** months already converted are skipped, so you can run the same command again.
- **Stockfish's judgement:** the scores are Stockfish's, so a net trained on them learns
  Stockfish's evaluation. That's the quick way to a strong first net, but it isn't tiki's own.
  Some rating lists and events care where a net's data came from, so check their rules if you plan
  to enter. Retraining on tiki's self-play once the net is in search makes later nets tiki's own.

#### Downloading Lichess dumps only (`lichess_download.py`)

Downloads the monthly dumps without converting them, e.g. overnight, so you can choose the
sampling later. The dumps keep the names `lichess.py` looks for, so `lichess.py` with the same
`--download-dir` converts them without downloading them again.

```bash
python3 lichess_download.py --download-dir /mnt/f/lichess --months 2025-10:2026-09
```

| Option | Default | |
|---|---|---|
| `--download-dir` | required | where the dumps go, about 27 GB a month in 2025-26 |
| `--months` | required | `YYYY-MM`, or ranges `YYYY-MM:YYYY-MM` |
| `--retries` | 10 | retries of a failed download before moving on to the next month |
| `--wait` | 60 | seconds before the first retry; each retry waits longer (60, 120, 180...) |
| `--reserve-gb` | 10 | free space to leave on the disk |

- **Re-running:** months already downloaded are skipped, and a partial download resumes.
- **Disk space:** before each month, the dump's size is checked against the free space, and the
  run stops rather than fill the disk.
- **Unpublished months** (HTTP 404, e.g. the current month) are reported and skipped.

#### Leela Chess Zero's self-play (`lc0.py`)

Lc0 publishes its training games at https://storage.lczero.org/files/training_data under the Open
Database License (ODbL 1.0: a net trained on them should credit "Leela Chess Zero training data,
ODbL", see the `LICENSE.txt` there). Each run adds a `.tar` of games about every hour, so there is
far more of it than of analysed Lichess games. The script streams each tar and converts it as it
arrives, one output file per tar, `--out-dir/lc0-<run>-<YYYYMMDD>-<HHMM>.bin`.

```bash
python3 lc0.py --out-dir /mnt/f/lc0/data --download-dir /mnt/f/lc0/tars \
               --run test91 --dates 2026-01-01:2026-07-01 --per-day 2 --workers 4
```

| Option | Default | |
|---|---|---|
| `--out-dir` | required | where the converted data goes |
| `--run` | `test80` | the training run, a directory on the server |
| `--dates` | required | `YYYY-MM-DD`, or ranges `YYYY-MM-DD:YYYY-MM-DD` |
| `--per-day` | 2 | tars per day, spread over it (0: all, about 24) |
| `--download-dir` | | keep the tars too, rather than only streaming them |
| `--skip-missing` | | only convert tars already in `--download-dir`, download nothing |
| `--skip-plies` | 16 | opening plies dropped (lc0 plays them with some randomness) |
| `--sample` | 0.25 | fraction of positions kept |
| `--max-score` | 3000 | largest score kept, in centipawns |
| `--q` | `best` | the score: lc0's value of its best move, or `root` / `played` |
| `--workers` | cores - 1 | conversion processes |
| `--seed` | 1 | sampling seed |

- **Runs:** check which runs are still producing data. test80's 2026 tars are empty; test91 has
  data from 2025-12-20, at about 0.3 GB a tar.
- **Positions:** lc0's records (V6) are from the side to move's point of view, written as the same
  position with White to move, which tiki's network evaluates identically. The result comes from the
  game.
- **Scores:** lc0's Q (wins - losses, -1..1) is converted to centipawns on the trainer's scale,
  `400 × ln((1 + q) / (1 - q))`, so the trainer's `sigmoid(score / 400)` reads it back as lc0's
  expected result. lc0's own display conversion gives about a third of that; data converted with it
  (before this change) trains nets whose evals are far too small, and should be converted again.

#### Checking score scales (`score_scale.py`)

Before training on several sources together, check that their scores mean the same thing:

```bash
python3 score_scale.py /mnt/f/lichess/data-s015 /mnt/f/lc0/data
```

For each data set it fits the scale `s` in `result ~ sigmoid(score / s)` (the trainer assumes 400),
and shows the average result by score band side by side. Sets whose `s` differ by more than 10-20%
shouldn't be mixed without rescaling. Lichess's Stockfish scores fit at about 424; lc0 data converted
with lc0's own formula fit at 139.
- **Downloads:** with `--download-dir`, each tar is downloaded first (with a progress bar, resuming
  a partial `.part` file) and kept, so it can be converted again later with other options.
- **Re-running:** tars already converted are skipped, so you can run the same command again.
- **Checks:** it stops if more than 1% of a tar's records aren't valid positions, which would mean
  the records aren't V6. Some runs mix in Chess960 games; their later positions are ordinary chess
  positions, and they're kept.
- **Lc0's judgement:** as with Lichess's Stockfish scores, a net trained on lc0's scores learns
  lc0's evaluation, with the same caveat for rating lists and events.

### 3. Train

```bash
pip install -r training/requirements.txt
cd training
python3 train.py --data ../data/*.txt --out nets --epochs 20
```

Or use `run_training.sh`, which runs the tests first and then trains on every `.txt` and `.bin`
file in one or more data directories, passing any other options to the trainer:

```bash
training/run_training.sh --data-dir /mnt/f/lichess/data-s015 --out /mnt/f/tiki/nets/s015 \
                         --epochs 6 --lr-drop 3
```

| `run_training.sh` option | |
|---|---|
| `--data-dir DIR` | a directory of data files, can be given more than once (e.g. Lichess and lc0 data) |
| `--out DIR` | where the checkpoints, networks and `train.log` go |
| `--streaming` | train with `train_streaming.py` (below) rather than `train.py` |
| `--skip-tests` | go straight to training |
| anything else | passed to the trainer |

Set `TIKI_APP` to the `tiki_app` binary to include the engine cross-check in the tests.

| Trainer option | Default | |
|---|---|---|
| `--data` | required | data files (`.bin` or `.txt`); directories too for `train_streaming.py` |
| `--out` | `nets` | where the checkpoints, networks and `train.log` go |
| `--epochs` | 20 | |
| `--lr` | 1e-3 | learning rate |
| `--lr-drop` | 10 | multiply the learning rate by 0.1 every N epochs |
| `--wdl` | 0.3 | weight of the game result against the search score |
| `--validation` | 0.02 | fraction held out for the validation loss, as whole games |
| `--batch-size` | 16384 | |
| `--workers` | 4 | processes converting `.txt` data to `.bin` |
| `--resume` | | a checkpoint (`.pt`) to continue from |
| `--device` | CUDA if available | |
| `--seed` | 1 | |

After each epoch this writes `<out>/tiki-<epoch>.pt` (a checkpoint, for `--resume`) and
`<out>/tiki-<epoch>.nnue` (the engine format). Progress bars show the loss, positions per second and
time remaining, and progress is also logged to `<out>/train.log` (every 10% of an epoch), so
`tail -f` it to check on a run from another terminal.

- **Binary data:** training reads `.bin` files (`tiki_nnue/binary.py`), 68 bytes per position:
  the pieces, the score, the result and the side to move. A `.txt` file is converted to a `.bin`
  beside it on first use, and again if the `.txt` changes. The features of each batch are computed
  on the GPU, so training never parses FENs.
- **Memory:** `train.py` loads all the data into memory, so 50M positions needs about 3.4 GB of RAM.
  Cap each month with `lichess.py --max-positions`, or stream it (below).
- **Validation:** it's held out in blocks of 4,096 consecutive positions, and the data files keep
  each game's positions together, so validation is whole games the network never trains on
  (positions from a game it trained on would make the validation loss look better than it is).
- **Choosing a net:** take the epoch with the lowest validation loss. Once the training loss keeps
  falling and the validation loss doesn't, more epochs won't help; more data will. On 71M Lichess
  positions, `--epochs 6 --lr-drop 3` was best at epoch 5, and the drop at epoch 3 gave the largest
  late gain.

#### Streaming (`train_streaming.py`)

`train_streaming.py` (or `run_training.sh --streaming`) trains on any amount of data with fixed
memory: months of Lichess data, lc0's and datagen's together.

```bash
python3 train_streaming.py --data /mnt/f/lichess/data /mnt/f/lc0/data --out nets --epochs 20
```

It takes the trainer options above, and:

| Option | Default | |
|---|---|---|
| `--buffer` | 8,000,000 | positions shuffled together (about 0.5 GB; two buffers are in memory) |
| `--epoch-positions` | 0 (a whole pass) | positions per epoch, for data too large to pass over often |
| `--validation-max` | 2,000,000 | positions validated after each epoch, at most (0: all held out) |

- `--data` takes files or directories (their `.bin` and `.txt` files; `.part` files of a
  conversion still running are left out).
- Each pass reads chunks of consecutive positions (about 17 MB) in a random order across all the
  files, and shuffles them in the buffer, so a batch mixes many games, months and sources. The next
  buffer is read in the background while the GPU trains. Reading from a Windows drive in WSL runs at
  over 1M positions a second, faster than training.
- Validation is whole games held out of each file, chosen from the file's name and `--seed`, so
  adding files later doesn't change what the others hold out.
- Checkpoints are the same as `train.py`'s, so either can `--resume` the other's.

### 4. Check a network

```bash
build/engine/app/tiki_app nnue-eval nets/tiki-20.nnue "<fen>"
```

This prints the evaluation from the side to move's point of view. The engine searches with the
network named by `TIKI_EVALFILE` (or the `EvalFile` UCI option), or the one built in:

```bash
TIKI_EVALFILE=nets/tiki-20.nnue build/engine/app/tiki_app bench
```

`tiki_app datagen` searches with it too, so self-play data then comes from tiki's own net.

### 5. Test it in games

A lower validation loss doesn't by itself mean a stronger engine, so play the new net against the
current one. `scripts/match.bat` (Windows) and `scripts/match.sh` (Linux, WSL and macOS) run
[fastchess](https://github.com/Disservin/fastchess), downloading it and an opening book into
`tools/` the first time. Games and results go to `matches/`.

#### SPRT: the new net against the current one

The same engine on both sides, each with its own network. The test plays until it's clear whether
the new net is at least `ELO1` (default 5) Elo stronger or not stronger at all:

```bat
cd C:\Users\lewis\develop\tiki
set NEW_EVALFILE=C:\Users\lewis\develop\tiki\nnue_candidate\tiki-5.nnue
set BASE_EVALFILE=C:\Users\lewis\develop\tiki\models\tiki-2026-10-23.nnue
scripts\match.bat sprt build-windows\engine\app\tiki_app.exe build-windows\engine\app\tiki_app.exe
```

In fastchess's report:

| Field | Meaning |
|---|---|
| `Elo` | the new net's Elo difference from the base, ± the 95% margin |
| `LOS` | likelihood of superiority: the chance the new net is genuinely stronger |
| `Ptnml(0-2)` | opening pairs (each opening played with both colours) scoring 0, 0.5, 1, 1.5 and 2 points for the new net |
| `LLR` | log-likelihood ratio: the evidence so far, between "stronger" and "not stronger". It wanders up and down; the test passes at +2.94 and fails at -2.94 |

The first few hundred games' Elo is noisy and usually drifts down; go by the LLR. For example,
`tiki-5.nnue` (Lichess 2026-01 to 2026-05) passed against `tiki-2026-10-23.nnue` after 1,690 games
at +33.6 ± 12.3 Elo.

#### Gauntlet: tiki against other engines

A fixed number of games against each engine (UCI only), for an Elo difference against engines with
a known rating:

```bat
set TIKI_EVALFILE=C:\Users\lewis\develop\tiki\nnue_candidate\tiki-5.nnue
set GAMES=400
scripts\match.bat gauntlet build-windows\engine\app\tiki_app.exe C:\Users\lewis\Downloads\arena_3.5.1\Engines\AnMon\AnMon_5.75.exe
```

- **Pick opponents tiki scores 25-75% against.** Above about 90% the Elo can't be measured: tiki
  scored 97.75% against AnMon 5.75, which only says it's at least about 450 Elo stronger.
- **Average several opponents.** A single opponent can skew the result by 50 Elo or more.
- **Rating lists use longer time controls.** At 8+0.08, a modern engine gains more over an older
  one than at a list's time control, so treat a rating from these games as approximate.
- **Quieter output:** some engines make fastchess print warnings for every move (`PV continues
  after threefold repetition`, for one). To hide them, pipe the output through:

  ```bat
  scripts\match.bat gauntlet ... | findstr /v /b /c:"Warning;" /c:"Info;" /c:"Position;" /c:"Moves;"
  ```

#### Settings

Set these in the environment first (`set NAME=value` in `cmd`, `NAME=value` before the command in
bash):

| Setting | Default | |
|---|---|---|
| `TC` | `8+0.08` | time control, seconds + increment |
| `CONCURRENCY` | physical cores - 1 | games at once |
| `HASH`, `THREADS` | 16, 1 | UCI options for every engine |
| `BOOK` | `tools/books/8moves_v3.pgn` | openings (`.epd` or `.pgn`) |
| `GAMES` | 200 | gauntlet: games against each engine |
| `ELO0`, `ELO1` | 0, 5 | SPRT bounds (normalized Elo) |
| `MAX_GAMES` | 20000 | SPRT: stop here if it's still undecided |
| `TIKI_EVALFILE` | the last network in `models/` by name | network for every tiki |
| `NEW_EVALFILE`, `BASE_EVALFILE` | | SPRT: a network for one side |

**Keep the machine otherwise idle**, or lower `CONCURRENCY`. At 8+0.08 a move gets a few tenths of
a second, and a busy machine (a data conversion running in WSL, say) makes engines lose on time for
reasons that have nothing to do with the network.

### 6. Promote it

Copy a net that passed its SPRT into `models/` with a dated name that sorts after the others, e.g.
`models/tiki-2026-10-24.nnue`. Embedded builds (`TIKI_EMBED_NETWORK`), the release workflow and
`match.bat` / `match.sh` all take the last network in `models/` by name. Keep the previous one as
the baseline for the next candidate.

### Tests

```bash
cd training
TIKI_APP=../build/engine/app/tiki_app python3 -m unittest discover -s tests
```

| Test | Checks |
|---|---|
| `test_features.py` | known indices (the same values the C tests use), range, uniqueness, and colour-flip and mirror symmetry |
| `test_model.py` | colour symmetry of the float model; converting white-relative scores to the side to move; the file round trip; the integer eval matching the float model on the quantised weights; a small training run reducing the loss |
| `test_cross_check.py` | exports a network and checks that `tiki_app nnue-eval` gives exactly `quantised_eval()`; skipped unless `TIKI_APP` is set |
| `test_data.py` | the batch features computed from binary records match `parse_line` and `collate`, including on positions from random games; converting text to binary (blank lines, CRLF, a missing final newline, any number of workers) and loading the result; rejecting files that aren't binary data or are truncated |
| `test_lichess.py` | converts a small `.pgn.zst` built in the test: only analysed games are parsed; the quiet-position, mate-score, opening, Elo, score and sample filters; the output reads back in the trainer, and the binary output is the text output encoded. Skipped without `python-chess` or `zstandard` |
| `test_lichess_download.py` | retrying failed downloads with longer pauses, giving up after the retries, not retrying an unpublished month; counting a partial download towards the space needed |
| `test_stream.py` | training and validation cover every record of every file once, and validation stays the same when files are added; a pass yields every training record once, mixed across files, and the next pass reorders them; epochs of a fixed number of batches, and the validation limit; a small `train_streaming.py` run that trains and exports |
| `test_lc0.py` | lc0 records built as lc0 encodes them decode to the real position's features, for either side to move; the policy move table against lc0's published one; the quiet-position filter (captures, en passant, promotions, check) and the other filters; rejecting records that aren't positions or V6; scores on the trainer's scale; downloading, resuming, and a server that ignores the resume request |
| `test_score_scale.py` | fitting the scale recovers a known one; sampling across files in proportion to their sizes |

### Next steps

- **lc0 data:** train on lc0's data alone and mixed with Lichess's, and SPRT each against the
  current net.
- **Data:**
  - multi-threaded datagen, writing the binary format directly;
  - a node limit instead of a depth limit.
- **Bigger nets:** a larger L1 and output buckets (by material), once there is enough data.
