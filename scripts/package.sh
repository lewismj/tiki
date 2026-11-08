#!/bin/sh
# Builds, tests and packages one release CPU level of tiki for x86-64 Linux:
#     scripts/package.sh <arch> [cmake options...]
#     scripts/package.sh x86-64-bmi2
#     scripts/package.sh x86-64-vnni512 -DTIKI_EMBED_NETWORK=ON
# Writes dist/tiki-v<version>-linux-<arch>.tar.gz (the version is ENGINE_VERSION in version.h)
# holding tiki-v<version>/{bin/tiki, models/<network>.nnue, scripts/run.sh, README.md, LICENSE}, and
# dist/bench-<arch>.txt, the packaged engine's bench output.
set -eu

arch=$1
shift
root=$(cd "$(dirname "$0")/.." && pwd)
# tr: a Windows checkout gives version.h CRLF line endings.
version=v$(sed -n 's/^#define ENGINE_VERSION *"\(.*\)".*/\1/p' "$root/engine/core/src/version.h" | tr -d '\r')
name=tiki-$version
build=$root/build-release/$arch
stage=$root/dist/stage-$arch

cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release -DTIKI_STATIC=ON -DTIKI_BUILD_BENCHMARKS=OFF "-DTIKI_ARCH=$arch" "$@"
cmake --build "$build" -j "$(nproc)"
(cd "$build" && ctest -LE slow --output-on-failure)

# A CPU level this machine doesn't run (CMake's TIKI_RUNS_<arch> check failed) is built, but its tests
# are disabled and it isn't benched.
probe=TIKI_RUNS_$(echo "$arch" | tr - _)
runs=1
if tr -d '\r' < "$build/CMakeCache.txt" | grep -q "^$probe:INTERNAL=" &&
   ! tr -d '\r' < "$build/CMakeCache.txt" | grep -q "^$probe:INTERNAL=1\$"; then
    runs=0
    echo "This machine doesn't run $arch code: built, not tested or benched"
fi

rm -rf "$stage"
mkdir -p "$stage/$name/bin" "$stage/$name/models" "$stage/$name/scripts"
cp "$build/engine/app/tiki" "$stage/$name/bin/tiki"
# Only the newest network of the format this engine reads (NNUE_VERSION in nnue.h), the one built in
# with TIKI_EMBED_NETWORK: older formats are for older engines.
nnue_version=$(sed -n 's/^#define NNUE_VERSION *\([0-9]*\).*/\1/p' "$root/engine/core/src/nnue/nnue.h" | tr -d '\r')
network=
for candidate in $(ls "$root"/models/*.nnue | sort -rV); do
    if [ "$(od -An -tu4 -j8 -N4 "$candidate" | tr -d ' \r\n')" = "$nnue_version" ]; then
        network=$candidate
        break
    fi
done
[ -n "$network" ] || { echo "No network of format version $nnue_version in models/" >&2; exit 1; }
cp "$network" "$stage/$name/models/"
cp "$root/scripts/run.sh" "$stage/$name/scripts/"
chmod +x "$stage/$name/bin/tiki" "$stage/$name/scripts/run.sh"
cp "$root/README.md" "$root/LICENSE" "$stage/$name/"

# ldd fails on a static binary ("not a dynamic executable").
if ldd "$stage/$name/bin/tiki" > /dev/null 2>&1; then
    echo "bin/tiki isn't statically linked" >&2
    exit 1
fi

# Runs the package itself, as a user would.
if [ $runs -eq 1 ]; then
    env -u TIKI_EVALFILE -u TIKI_APP "$stage/$name/scripts/run.sh" bench > "$root/dist/bench-$arch.txt"
    grep "Nodes searched" "$root/dist/bench-$arch.txt"
fi

tar -C "$stage" -czf "$root/dist/$name-linux-$arch.tar.gz" "$name"
rm -rf "$stage"
echo "Wrote dist/$name-linux-$arch.tar.gz"
