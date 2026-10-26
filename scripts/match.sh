#!/bin/sh
# Plays engine matches with fastchess (Linux, WSL or macOS; scripts/match.bat is the Windows version).
#
#     scripts/match.sh sprt <new engine> <base engine>
#         SPRT: plays the new build against the base build until it's clear whether the new one is
#         stronger, e.g. a feature branch's tiki against main's.
#     scripts/match.sh gauntlet <tiki> <engine>...
#         Plays tiki against each engine for a fixed number of games and reports the Elo difference.
#
# fastchess (the latest release) and the opening book are downloaded into tools/ the first time.
# Games and results go to matches/. Settings, from the environment:
#     TC=8+0.08            time control, seconds+increment
#     CONCURRENCY=N        games at once, default physical cores - 1
#     HASH=16 THREADS=1    UCI options for every engine
#     BOOK=<file>          openings (.epd or .pgn), default tools/books/8moves_v3.pgn
#     GAMES=200            gauntlet: games against each engine
#     ELO0=0 ELO1=5        SPRT bounds (normalized Elo)
#     MAX_GAMES=20000      SPRT: stop here if it's still undecided
#     TIKI_EVALFILE        network for every tiki, default the newest in models/
#     NEW_EVALFILE BASE_EVALFILE  SPRT: a different network for one side (to test networks)
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
tools=$root/tools
usage() {
    sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 1
}
[ $# -ge 3 ] || usage
mode=$1
shift

# The absolute path of an engine, which must exist.
engine_path() {
    [ -f "$1" ] || { echo "No engine at $1" >&2; exit 1; }
    echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
}

# fastchess, the latest release for this platform.
case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) platform=linux-x86-64 ;;
    Linux-aarch64) platform=linux-arm64 ;;
    Darwin-arm64) platform=mac-arm64 ;;
    Darwin-x86_64) platform=mac-x86-64 ;;
    *) echo "No fastchess release for $(uname -s) $(uname -m), on Windows use scripts\\match.bat" >&2; exit 1 ;;
esac
fastchess=$tools/fastchess-$platform/fastchess
if [ ! -x "$fastchess" ]; then
    mkdir -p "$tools"
    echo "Downloading fastchess ($platform)"
    curl -fsSL -o "$tools/fastchess.tar" \
        "https://github.com/Disservin/fastchess/releases/latest/download/fastchess-$platform.tar"
    tar -C "$tools" -xf "$tools/fastchess.tar" "fastchess-$platform/fastchess" "fastchess-$platform/LICENSE"
    rm "$tools/fastchess.tar"
fi

# The opening book.
book=${BOOK:-$tools/books/8moves_v3.pgn}
if [ ! -f "$book" ] && [ -z "${BOOK:-}" ]; then
    mkdir -p "$tools/books"
    echo "Downloading the opening book"
    curl -fsSL -o "$book.zip" "https://github.com/official-stockfish/books/raw/master/8moves_v3.pgn.zip"
    if command -v unzip > /dev/null; then
        unzip -oq "$book.zip" -d "$tools/books"
    else
        python3 -m zipfile -e "$book.zip" "$tools/books"
    fi
    rm "$book.zip"
fi
[ -f "$book" ] || { echo "No opening book at $book" >&2; exit 1; }
case "$book" in
    *.epd) book_format=epd ;;
    *) book_format=pgn ;;
esac

# tiki loads TIKI_EVALFILE at startup, fastchess passes the environment on to the engines.
if [ -z "${TIKI_EVALFILE:-}" ]; then
    TIKI_EVALFILE=$(ls "$root"/models/*.nnue 2> /dev/null | sort -V | tail -n 1)
fi
export TIKI_EVALFILE

if [ -z "${CONCURRENCY:-}" ]; then
    if command -v lscpu > /dev/null; then
        cores=$(lscpu -p=Core,Socket | grep -v '^#' | sort -u | wc -l)
    else
        cores=$(sysctl -n hw.physicalcpu 2> /dev/null || echo 2)
    fi
    CONCURRENCY=$((cores > 1 ? cores - 1 : 1))
fi

mkdir -p "$root/matches"
name=$mode-$(date +%Y%m%d-%H%M%S)

# The engines' fastchess options replace the arguments, then the options common to both modes follow.
case "$mode" in
    sprt)
        [ $# -eq 2 ] || usage
        new=$(engine_path "$1")
        base=$(engine_path "$2")
        echo "SPRT [${ELO0:-0}, ${ELO1:-5}]: $new vs $base, $CONCURRENCY at once, network $TIKI_EVALFILE"
        set -- -engine "cmd=$new" name=new ${NEW_EVALFILE:+"option.EvalFile=$NEW_EVALFILE"} \
               -engine "cmd=$base" name=base ${BASE_EVALFILE:+"option.EvalFile=$BASE_EVALFILE"} \
               -rounds $((${MAX_GAMES:-20000} / 2)) \
               -sprt elo0="${ELO0:-0}" elo1="${ELO1:-5}" alpha=0.05 beta=0.05 model=normalized
        ;;
    gauntlet)
        tiki=$(engine_path "$1")
        shift
        echo "Gauntlet: $tiki vs $*, ${GAMES:-200} games each, $CONCURRENCY at once, network $TIKI_EVALFILE"
        # Each opponent's options go on the end, then it's shifted off the front.
        opponents=$#
        set -- "$@" -tournament gauntlet -seeds 1 -engine "cmd=$tiki" name=tiki -rounds $((${GAMES:-200} / 2))
        while [ "$opponents" -gt 0 ]; do
            path=$(engine_path "$1")
            shift
            set -- "$@" -engine "cmd=$path" "name=$(basename "$path")"
            opponents=$((opponents - 1))
        done
        ;;
    *)
        usage
        ;;
esac

exec "$fastchess" "$@" \
    -openings "file=$book" format="$book_format" order=random -repeat \
    -each tc="${TC:-8+0.08}" option.Hash="${HASH:-16}" option.Threads="${THREADS:-1}" \
    -concurrency "$CONCURRENCY" -recover \
    -draw movenumber=40 movecount=8 score=10 -resign movecount=3 score=600 twosided=true \
    -pgnout "file=$root/matches/$name.pgn" -config "outname=$root/matches/$name.json"
