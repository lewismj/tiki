#!/usr/bin/env bash
# Downloads Fishtest's game PGNs (Hugging Face official-stockfish/fishtest_pgns) a month at a time, converts
# each month with 'lichess_convert --fishtest' into one TIKIBIN1 file, then deletes that month's PGNs, so the
# disk only ever holds one month of PGNs beside the converted data.
#
#   training/fishtest_convert.sh <year 2 digits> <month> [<month> ...]
#   e.g. training/fishtest_convert.sh 26 01 02 03 04 05 06 07 08 09 10 11
#
# Output: $OUT/fishtest-20YY-MM.bin (a month already converted is skipped). Run from WSL; the download uses the
# Windows Hugging Face CLI ($HF, hf.exe), which writes to $PGN_WIN (the Windows path of $PGN). Extra options for
# lichess_convert (e.g. --sample 0.5) go in $CONVERT_OPTIONS.
#
# The folders are named YY-MM-DD, so a month is the pattern 'YY-MM-*'. The current month is incomplete: convert
# it again later (delete its .bin first) to pick up the rest.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HF="${HF:-/mnt/c/Python/Python313/Scripts/hf.exe}"
PGN="${PGN:-/mnt/f/fishtest}"
PGN_WIN="${PGN_WIN:-F:\\fishtest}"
OUT="${OUT:-/mnt/f/fishtest-data}"
CONVERT="$here/lichess_convert/lichess_convert"
CONVERT_OPTIONS="${CONVERT_OPTIONS:-}"

if [ $# -lt 2 ]; then sed -n '2,15s/^# \{0,1\}//p' "$0"; exit 1; fi
year="$1"; shift
mkdir -p "$OUT"

for month in "$@"; do
    out="$OUT/fishtest-20$year-$month.bin"
    if [ -e "$out" ]; then echo "20$year-$month: $out exists, skipped"; continue; fi
    echo "20$year-$month: downloading"
    "$HF" download official-stockfish/fishtest_pgns --repo-type dataset \
        --include "$year-$month-*/*/*.pgn.gz" --local-dir "$PGN_WIN" > /dev/null
    shopt -s nullglob
    files=("$PGN"/"$year-$month"-*/*/*.pgn.gz)
    shopt -u nullglob
    if [ ${#files[@]} -eq 0 ]; then echo "20$year-$month: no games"; continue; fi
    size=$(du -ch "${files[@]}" | tail -1 | cut -f1)
    echo "20$year-$month: ${#files[@]} files, $size; converting"
    # shellcheck disable=SC2086
    zcat "${files[@]}" | "$CONVERT" --fishtest $CONVERT_OPTIONS /dev/stdin "$out"
    rm -rf "$PGN"/"$year-$month"-*
    echo "20$year-$month: done, $(du -h "$out" | cut -f1)"
done
