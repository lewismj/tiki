# syzygy_relabel

Replaces the game result of tiki's training positions (`TIKIBIN1`, see `../tiki_nnue/binary.py`) with the
Syzygy tablebase result, for the positions the tables cover. The score (the search's) is kept; only the result
byte changes. Lichess games are often won or lost from tablebase draws, and drawn from won positions, so in the
endgame the game result is often wrong: in 2026-09, 30% of the positions with at most 5 pieces.

The tables are probed with [Fathom](https://github.com/jdart1/Fathom) (MIT licence), which the Makefile
downloads at a fixed commit into `build/`.

## Build and test

In WSL or Linux:

```bash
cd training/syzygy_relabel
make                                  # ./syzygy_relabel
make test TB=/mnt/f/syzygy/wdl        # random positions against python-chess's Syzygy prober
python3 tests/compare.py ./syzygy_relabel --tb /mnt/f/syzygy/wdl \
        --bin /mnt/f/lichess2/data-2000-s100/lichess-2026-09.bin --records 2000000   # real records
```

The tests relabel the same records with this tool and with python-chess (`chess.syzygy`, an independent
implementation) and compare the outputs byte for byte, with one and with three threads.

## Use

```bash
# The whole data set into a new directory (each file via .part; existing outputs are skipped)
./syzygy_relabel --tb /mnt/f/syzygy/wdl /mnt/f/lichess2/data-2000-s100 /mnt/f/lichess2/data-2000-s100-tb

# Count what would change, writing nothing
./syzygy_relabel --tb /mnt/f/syzygy/wdl --stats-only /mnt/f/lichess2/data-2000-s100/lichess-2026-09.bin
```

| Option | Default | |
|---|---|---|
| `--tb DIR` | required | Syzygy WDL tables (`.rtbw`), several directories separated by `:` |
| `--max-pieces N` | largest tables found | relabel positions with at most N pieces |
| `--threads N` | 4 | probing threads |
| `--stats-only` | | count what would change, write nothing |
| `--force` | | rewrite outputs that already exist |

It prints, per file and in total, the positions probed and changed, a table of game result against tablebase
result, and the changes by piece count.

## What a record doesn't hold

- **Castling rights.** The tables don't cover positions that can castle. A position with a king and rook of the
  same colour on their starting squares might, so it's left unchanged (51 positions in 2026-09).
- **The en passant square.** When an en passant capture might be available, the position is probed with and
  without it, and left unchanged if the results differ (405 in 2026-09).
- **The fifty-move counter.** Positions are probed as if it were zero. A win the fifty-move rule turns into a
  draw ("cursed win", or "blessed loss" for the loser) is labelled a draw.

Train on the relabelled directory instead of the original, not as well: the two hold the same positions.
