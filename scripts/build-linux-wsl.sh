#!/bin/sh
# Builds tiki (Release) on Linux or in WSL:
#     scripts/build-linux-wsl.sh [options] [-- cmake options...]
#     scripts/build-linux-wsl.sh --embed --test
#     scripts/build-linux-wsl.sh --cc clang-22 --variant avx2 -- -DTIKI_SANITIZE=address,undefined
# Every run sets the options below, so a build directory doesn't keep them from an earlier run.
usage() {
    cat <<EOF
Usage: scripts/build-linux-wsl.sh [options] [-- cmake options...]
  --embed           Build the newest network in models/ into the engine (runs without a .nnue file).
  --net <file>      Build this network into the engine (implies --embed).
  --variant <v>     native (default, what this machine supports), bmi2 (AVX2 and PEXT), avx2 (AVX2, no
                    PEXT, for AMD Zen 1/2) or x86-64 (neither), as the release packages.
  --cc <compiler>   C compiler, e.g. clang-22 (default \$CC, else clang). C++ follows it (clang-22 -> clang++-22).
  --static          Link statically, as the release packages.
  --benchmarks      Also build the Google Benchmark micro-benchmarks.
  --test            Run the unit tests and bench smoke test after building.
  --clean           Delete the build directory first (needed to change compiler).
  --dir <dir>       Build directory (default build-linux, relative to the repository).
  -h, --help        This help.
EOF
}
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
dir=build-linux
variant=native
cc=${CC:-clang}
embed=OFF
net=
static=OFF
benchmarks=OFF
test=0
clean=0

while [ $# -gt 0 ]; do
    case $1 in
        --embed) embed=ON ;;
        --net) [ $# -ge 2 ] || { usage >&2; exit 2; }; embed=ON; net=$(cd "$(dirname "$2")" && pwd)/$(basename "$2"); shift ;;
        --variant) [ $# -ge 2 ] || { usage >&2; exit 2; }; variant=$2; shift ;;
        --cc) [ $# -ge 2 ] || { usage >&2; exit 2; }; cc=$2; shift ;;
        --static) static=ON ;;
        --benchmarks) benchmarks=ON ;;
        --test) test=1 ;;
        --clean) clean=1 ;;
        --dir) [ $# -ge 2 ] || { usage >&2; exit 2; }; dir=$2; shift ;;
        -h|--help) usage; exit 0 ;;
        --) shift; break ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

case $variant in
    native) cpu="-UTIKI_USE_AVX2 -UTIKI_USE_PEXT" ;;    # Defaults from this machine again.
    bmi2)   cpu="-DTIKI_USE_AVX2=ON -DTIKI_USE_PEXT=ON" ;;
    avx2)   cpu="-DTIKI_USE_AVX2=ON -DTIKI_USE_PEXT=OFF" ;;
    x86-64) cpu="-DTIKI_USE_AVX2=OFF -DTIKI_USE_PEXT=OFF" ;;
    *) echo "Unknown variant: $variant" >&2; usage >&2; exit 2 ;;
esac

case $cc in
    *clang*) cxx=$(echo "$cc" | sed 's/clang/clang++/') ;;
    *gcc*)   cxx=$(echo "$cc" | sed 's/gcc/g++/') ;;
    *)       cxx=${CXX:-c++} ;;
esac

case $dir in
    /*) build=$dir ;;
    *)  build=$root/$dir ;;
esac
[ $clean -eq 1 ] && rm -rf "$build"

generator=
command -v ninja > /dev/null && generator="-G Ninja"

# $cpu and $generator are word split on purpose.
cmake -S "$root" -B "$build" $generator -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" $cpu \
      -DTIKI_EMBED_NETWORK=$embed -DTIKI_EMBED_NETWORK_FILE="$net" \
      -DTIKI_STATIC=$static -DTIKI_BUILD_BENCHMARKS=$benchmarks "$@"
cmake --build "$build" -j "$(nproc)"
[ $test -eq 1 ] && (cd "$build" && ctest -LE slow --output-on-failure)

echo
echo "Built $build/engine/app/tiki"
if [ $embed = ON ]; then
    echo "The network is built in, it runs on its own (TIKI_EVALFILE still overrides it)."
else
    echo "Set TIKI_EVALFILE to a .nnue file to run it (or build with --embed)."
fi
