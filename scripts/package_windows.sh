#!/bin/sh
# Builds, tests and packages one release variant of tiki for x86-64 Windows (run from Git Bash,
# with LLVM clang, CMake and Ninja on the PATH, see 'Windows' in the main README):
#     scripts/package_windows.sh <variant> [cmake options...]
#     scripts/package_windows.sh x86-64-bmi2 -DTIKI_USE_AVX2=ON -DTIKI_USE_PEXT=ON
# Writes dist/tiki-v<version>-windows-<variant>.zip (the version is ENGINE_VERSION in version.h)
# holding tiki-v<version>/{bin/tiki.exe, models/*.nnue, scripts/run.bat, README.md, LICENSE}, and
# dist/bench-windows-<variant>.txt, the packaged engine's bench output.
set -eu

variant=$1
shift
root=$(cd "$(dirname "$0")/.." && pwd)
version=v$(sed -n 's/^#define ENGINE_VERSION *"\(.*\)".*/\1/p' "$root/engine/core/src/version.h" | tr -d '\r')
name=tiki-$version
build=$root/build-release/windows-$variant
stage=$root/dist/stage-windows-$variant

cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang \
      -DCMAKE_CXX_COMPILER=clang++ -DTIKI_BUILD_BENCHMARKS=OFF "$@"
cmake --build "$build"
(cd "$build" && ctest -LE slow --output-on-failure)

rm -rf "$stage"
mkdir -p "$stage/$name/bin" "$stage/$name/models" "$stage/$name/scripts"
cp "$build/engine/app/tiki_app.exe" "$stage/$name/bin/tiki.exe"
cp "$root"/models/*.nnue "$stage/$name/models/"
cp "$root/scripts/run.bat" "$stage/$name/scripts/"
cp "$root/README.md" "$root/LICENSE" "$stage/$name/"

# The CRT is linked statically, so the only DLL should be Windows' own KERNEL32.
dlls=$(llvm-objdump -p "$stage/$name/bin/tiki.exe" | sed -n 's/^ *DLL Name: //p' | tr -d '\r')
if [ "$dlls" != "KERNEL32.dll" ]; then
    echo "bin/tiki.exe needs DLLs other than KERNEL32.dll:" $dlls >&2
    exit 1
fi

# Runs the package itself, as a user would.
(unset TIKI_EVALFILE TIKI_APP; cmd //c "$(cygpath -w "$stage/$name/scripts/run.bat")" bench) > "$root/dist/bench-windows-$variant.txt"
grep "Nodes searched" "$root/dist/bench-windows-$variant.txt"

rm -f "$root/dist/$name-windows-$variant.zip"
(cd "$stage" && powershell -NoProfile -Command "Compress-Archive -Path '$name' -DestinationPath '$(cygpath -w "$root/dist/$name-windows-$variant.zip")'")
rm -rf "$stage"
echo "Wrote dist/$name-windows-$variant.zip"
