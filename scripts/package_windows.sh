#!/bin/sh
# Builds, tests and packages one release CPU level of tiki for x86-64 Windows (run from Git Bash,
# with LLVM clang, CMake and Ninja on the PATH, see 'Windows' in the main README):
#     scripts/package_windows.sh <arch> [cmake options...]
#     scripts/package_windows.sh x86-64-bmi2
#     scripts/package_windows.sh x86-64-vnni512 -DTIKI_EMBED_NETWORK=ON
# Writes dist/tiki-v<version>-windows-<arch>.zip (the version is ENGINE_VERSION in version.h)
# holding tiki-v<version>/{bin/tiki.exe, models/<network>.nnue, scripts/run.bat, README.md, LICENSE}, and
# dist/bench-windows-<arch>.txt, the packaged engine's bench output.
set -eu

arch=$1
shift
root=$(cd "$(dirname "$0")/.." && pwd)
version=v$(sed -n 's/^#define ENGINE_VERSION *"\(.*\)".*/\1/p' "$root/engine/core/src/version.h" | tr -d '\r')
name=tiki-$version
build=$root/build-release/windows-$arch
stage=$root/dist/stage-windows-$arch

cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang \
      -DCMAKE_CXX_COMPILER=clang++ -DTIKI_BUILD_BENCHMARKS=OFF "-DTIKI_ARCH=$arch" "$@"
cmake --build "$build"
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
cp "$build/engine/app/tiki.exe" "$stage/$name/bin/tiki.exe"
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
cp "$root/scripts/run.bat" "$stage/$name/scripts/"
cp "$root/README.md" "$root/LICENSE" "$stage/$name/"

# The CRT is linked statically, so the only DLL should be Windows' own KERNEL32.
dlls=$(llvm-objdump -p "$stage/$name/bin/tiki.exe" | sed -n 's/^ *DLL Name: //p' | tr -d '\r')
if [ "$dlls" != "KERNEL32.dll" ]; then
    echo "bin/tiki.exe needs DLLs other than KERNEL32.dll:" $dlls >&2
    exit 1
fi

# Runs the package itself, as a user would.
if [ $runs -eq 1 ]; then
    (unset TIKI_EVALFILE TIKI_APP; cmd //c "$(cygpath -w "$stage/$name/scripts/run.bat")" bench) > "$root/dist/bench-windows-$arch.txt"
    grep "Nodes searched" "$root/dist/bench-windows-$arch.txt"
fi

rm -f "$root/dist/$name-windows-$arch.zip"
(cd "$stage" && powershell -NoProfile -Command "Compress-Archive -Path '$name' -DestinationPath '$(cygpath -w "$root/dist/$name-windows-$arch.zip")'")
rm -rf "$stage"
echo "Wrote dist/$name-windows-$arch.zip"
