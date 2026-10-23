#!/bin/sh
# Builds, tests and packages one release variant of tiki for x86-64 Linux:
#     scripts/package.sh <variant> [cmake options...]
#     scripts/package.sh x86-64-bmi2 -DTIKI_USE_AVX2=ON -DTIKI_USE_PEXT=ON
# Writes dist/tiki-v<version>-linux-<variant>.tar.gz (the version is ENGINE_VERSION in version.h)
# holding tiki-v<version>/{bin/tiki, models/*.nnue, scripts/run.sh, README.md, LICENSE}, and
# dist/bench-<variant>.txt, the packaged engine's bench output.
set -eu

variant=$1
shift
root=$(cd "$(dirname "$0")/.." && pwd)
# tr: a Windows checkout gives version.h CRLF line endings.
version=v$(sed -n 's/^#define ENGINE_VERSION *"\(.*\)".*/\1/p' "$root/engine/core/src/version.h" | tr -d '\r')
name=tiki-$version
build=$root/build-release/$variant
stage=$root/dist/stage-$variant

cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release -DTIKI_STATIC=ON -DTIKI_BUILD_BENCHMARKS=OFF "$@"
cmake --build "$build" -j "$(nproc)"
(cd "$build" && ctest -LE slow --output-on-failure)

rm -rf "$stage"
mkdir -p "$stage/$name/bin" "$stage/$name/models" "$stage/$name/scripts"
cp "$build/engine/app/tiki_app" "$stage/$name/bin/tiki"
cp "$root"/models/*.nnue "$stage/$name/models/"
cp "$root/scripts/run.sh" "$stage/$name/scripts/"
chmod +x "$stage/$name/bin/tiki" "$stage/$name/scripts/run.sh"
cp "$root/README.md" "$root/LICENSE" "$stage/$name/"

# ldd fails on a static binary ("not a dynamic executable").
if ldd "$stage/$name/bin/tiki" > /dev/null 2>&1; then
    echo "bin/tiki isn't statically linked" >&2
    exit 1
fi

# Runs the package itself, as a user would.
env -u TIKI_EVALFILE -u TIKI_APP "$stage/$name/scripts/run.sh" bench > "$root/dist/bench-$variant.txt"
grep "Nodes searched" "$root/dist/bench-$variant.txt"

tar -C "$stage" -czf "$root/dist/$name-linux-$variant.tar.gz" "$name"
rm -rf "$stage"
echo "Wrote dist/$name-linux-$variant.tar.gz"
