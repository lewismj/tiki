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
| `-DTIKI_USE_PEXT=OFF` | magic bitboards instead of PEXT (on by default with BMI2; turn it off on AMD Zen 1/2, where PEXT is slow) |
| `-DTIKI_USE_AVX2=OFF` | plain C for the network instead of AVX2 (same evaluations) |
| `-DTIKI_TUNE=ON` | the tunable search parameters become UCI options, for SPSA ([testing.md](testing.md#spsa-tuning)) |
| `-DTIKI_SEARCH_STATS=ON` | counts move ordering statistics, printed by `bench` |
| `-DTIKI_SANITIZE=address,undefined` | sanitizers (or `thread`) |
| `-DTIKI_BUILD_TESTS=OFF` | no unit tests (and no Unity download) |
| `-DTIKI_BUILD_BENCHMARKS=OFF` | no Google Benchmark |

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
`.github/workflows/release.yml`, which publishes a GitHub release with a package per CPU variant (`x86-64-bmi2`,
`x86-64-avx2`, `x86-64`) and platform. It's only published if all six packages give the same bench signature.
Running the workflow by hand builds the packages as an artifact without releasing.

- **Linux** packages unpack to `tiki-v<version>/`: `bin/tiki` (statically linked), the networks from `models/`, and
  `scripts/run.sh`, which starts tiki with the last network by name (`TIKI_EVALFILE` overrides it).
- **Windows** packages (`.zip`) hold `bin/tiki.exe` and `scripts/run.bat`, which does the same.
- **A built-in network:** tick `embed_network` when running the workflow by hand, or set the repository variable
  `TIKI_EMBED_NETWORK` to `true` for tag pushes.

`scripts/package.sh <variant> [cmake options]` builds, tests and packages one variant locally into `dist/`, and
`scripts/package_windows.sh` does the same on Windows (from Git Bash).
