#!/usr/bin/env bash
# Runs the trainer's tests, then trains on every .txt and .bin file in the data directories.
#
#   training/run_training.sh --data-dir /mnt/f/lichess/data --out /mnt/f/tiki/nets [train.py options]
#
# --data-dir can be given more than once (e.g. Lichess data and tiki's own datagen output).
# Anything else is passed to train.py (--epochs, --batch-size, --wdl, --lr, --resume, ...).
# Progress is logged to <out>/train.log as well ('tail -f' it from another terminal). Set TIKI_APP
# to the tiki_app binary to include the engine cross-check in the tests, and use --skip-tests to go
# straight to training.

set -euo pipefail

usage() {
    sed -n '2,11s/^# \{0,1\}//p' "$0"
}

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
data_dirs=()
out_dir=""
skip_tests=""
extra=()

while [ $# -gt 0 ]; do
    case "$1" in
        --data-dir) data_dirs+=("$2"); shift 2 ;;
        --out) out_dir="$2"; shift 2 ;;
        --skip-tests) skip_tests=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) extra+=("$1"); shift ;;
    esac
done

if [ ${#data_dirs[@]} -eq 0 ] || [ -z "$out_dir" ]; then
    usage
    exit 1
fi

shopt -s nullglob
files=()
for dir in "${data_dirs[@]}"; do
    files+=("$dir"/*.txt "$dir"/*.bin)
done
if [ ${#files[@]} -eq 0 ]; then
    echo "No .txt or .bin data files in: ${data_dirs[*]}" >&2
    exit 1
fi

cd "$here"
if [ -z "$skip_tests" ]; then
    echo "Running the trainer's tests..."
    python3 -m unittest discover -s tests
fi

mkdir -p "$out_dir"
echo "Found ${#files[@]} data file(s), writing to $out_dir"
python3 train.py --data "${files[@]}" --out "$out_dir" "${extra[@]}"
