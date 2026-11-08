# Building

Needs CMake 3.24+ and GCC or Clang with C23. Unity (the C tests) and Google Benchmark are fetched at configure time.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && ctest -LE slow        # unit tests and the bench smoke test; 'ctest' also runs the full perft file
```

Or `scripts/build-linux-wsl.sh` (Linux or WSL) and `scripts\build-windows.bat` (Windows), which take the options
below as flags, e.g. `--embed --test` (see [scripts/README.md](../scripts/README.md), or `--help`).

The unit tests build a small material-counting network in code, so they don't need a network file. The bench smoke
test runs `tiki`, so it's skipped unless `TIKI_EVALFILE` is set or a network is built in.

## Options

| CMake option | |
|---|---|
| `-DTIKI_EMBED_NETWORK=ON` | builds a network into `tiki`, so it runs with no `.nnue` file: the newest in `models/` by the date in its name, or `-DTIKI_EMBED_NETWORK_FILE=<path>`. The binary grows by the network's size and evaluates exactly as with the file. |
| `-DTIKI_ARCH=<level>` | the CPU level, Stockfish's names and flags: `native` (default, the best the building machine runs), `general-64` (plain C), `x86-64`, `x86-64-sse41-popcnt`, `x86-64-avx2`, `x86-64-bmi2`, `x86-64-avxvnni`, `x86-64-avx512`, `x86-64-vnni512`, `x86-64-avx512icl`. See below. |
| `-DTIKI_TUNE=ON` | the tunable search parameters become UCI options, for SPSA ([testing.md](testing.md#spsa-tuning)) |
| `-DTIKI_SEARCH_STATS=ON` | counts move ordering statistics, printed by `bench` |
| `-DTIKI_SANITIZE=address,undefined` | sanitizers (or `thread`) |
| `-DTIKI_BUILD_TESTS=OFF` | no unit tests (and no Unity download) |
| `-DTIKI_BUILD_BENCHMARKS=OFF` | no Google Benchmark |

### CPU levels

`TIKI_ARCH` follows Stockfish's `ARCH` (its Makefile), names and compiler flags, each level including the ones
before it. The flags apply to the whole build, so a binary runs only on CPUs of its level or above.

| Level | Adds | Network code |
|---|---|---|
| `general-64` | nothing CPU specific | plain C |
| `x86-64` | SSE2 | SSE2 (plain C hidden layer) |
| `x86-64-sse41-popcnt` | SSSE3, SSE4.1, POPCNT | SSSE3 |
| `x86-64-avx2` | AVX2, BMI1 (magic bitboards: PEXT is slow on AMD Zen 1/2) | AVX2 |
| `x86-64-bmi2` | BMI2, PEXT slider attacks | AVX2 |
| `x86-64-avxvnni` | AVX-VNNI (Intel Alder Lake and later) | AVX2 with VNNI dot products |
| `x86-64-avx512` | `x86-64-bmi2` + AVX-512 F/BW/DQ/VL | AVX-512 |
| `x86-64-vnni512` | AVX-512 VNNI (Intel Ice Lake, Tiger Lake and later, AMD Zen 4 and later) | AVX-512 with VNNI |
| `x86-64-avx512icl` | the rest of Ice Lake's AVX-512 extensions | AVX-512 with VNNI |

`native` tries the levels from the top on the building machine (a test program per level) and takes the first
that runs; on an AMD CPU before Zen 3 it stops at `x86-64-avx2`, as PEXT is slow there. Every level gives exactly
the same evaluations and bench (integer arithmetic, and `-ffp-contract=off` so AVX-512's FMA can't change a
floating point result). A level the building machine can't run is built but its tests aren't run.

Use Release (or RelWithDebInfo). Debug (`-O0`) doesn't build yet, because some `always_inline` functions are called
through function pointers in the move generator.

Without a built-in network, tiki needs one at startup: set `TIKI_EVALFILE` to a `.nnue` file, or use the
`EvalFile` option. Its default is the network in use; for a built-in one that's its name, which selects it again.

## Windows

Builds with LLVM clang targeting the MSVC ABI (`clang`/`clang++`, not `cl` or `clang-cl`), with the Visual Studio
Windows SDK and linker (tested with Visual Studio 2026 and LLVM 22) and Ninja. The bench signature matches the
Linux build.

```bat
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build-win
```

`scripts\build-windows.bat` does this, finding Visual Studio's CMake and Ninja if they aren't on the `PATH`.
`engine/platform/win32` provides the POSIX headers tiki uses (`pthread.h`, `unistd.h`, `strings.h`,
`sys/time.h`) over Win32. The CRT is linked statically, so `tiki.exe` needs no Visual C++ redistributable.

## `make` (OpenBench)

The root `Makefile` builds just the engine, with the network built in, the way
[OpenBench](https://github.com/AndyGrant/OpenBench) expects (Linux, macOS or WSL; it wraps CMake):

```bash
make EXE=tiki-dev [CC=clang] [EVALFILE=path/to/net.nnue] [TUNE=1]
```

The network is `EVALFILE` (OpenBench passes it for an uploaded network) or the newest in `models/`. Nothing is
downloaded. See [testing.md](testing.md#openbench) for the server setup.

## Releases

Pushing a tag `v<version>` matching `ENGINE_VERSION` in `engine/core/src/version.h` (e.g. `v0.0.1`) runs
`.github/workflows/release.yml`, which publishes a GitHub release with a package per CPU level (Stockfish's eight
x86-64 levels, above) and platform. It's only published if every package the runners can run gives the same bench
signature.
Running the workflow by hand builds the packages as an artifact without releasing.

- **Linux** packages unpack to `tiki-v<version>/`: `bin/tiki` (statically linked), the newest network from `models/` of
  the format it reads, and `scripts/run.sh`, which starts tiki with it (`TIKI_EVALFILE` overrides it).
- **Windows** packages (`.zip`) hold `bin/tiki.exe` and `scripts/run.bat`, which does the same.
- **A built-in network:** every build, by hand or from a tag, has the newest network in `models/` built in, so
  `bin/tiki` and `bin/tiki.exe` run from a GUI without the run scripts (`TIKI_EVALFILE` or `EvalFile` override it).

`scripts/package.sh <level> [cmake options]` builds, tests and packages one level locally into `dist/`, and
`scripts/package_windows.sh` does the same on Windows (from Git Bash).
