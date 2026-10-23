#!/bin/sh
# Starts tiki from a release package (tiki-<version>/scripts/run.sh), with the network shipped in
# tiki-<version>/models. Arguments are passed on, e.g. 'run.sh bench'. Set TIKI_EVALFILE to use
# another network, or TIKI_APP to use another binary.
here=$(cd "$(dirname "$0")/.." && pwd)

if [ -z "$TIKI_EVALFILE" ]; then
    # The last network in version order, i.e. the newest with dated names (tiki-2026-11-05.nnue
    # over tiki-2026-10-23.nnue).
    TIKI_EVALFILE=$(ls "$here"/models/*.nnue 2>/dev/null | sort -V | tail -n 1)
    if [ -z "$TIKI_EVALFILE" ]; then
        echo "No network in $here/models, set TIKI_EVALFILE to one." >&2
        exit 1
    fi
fi
export TIKI_EVALFILE

exec "${TIKI_APP:-$here/bin/tiki}" "$@"
