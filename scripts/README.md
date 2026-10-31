### Scripts

-- *build-linux-wsl.sh* Builds tiki (Release) on Linux or in WSL, into `build-linux/`. Options (`--help` lists them):
   `--embed` builds the newest network in `models/` in (`--net <file>` a given one), `--variant
   native|bmi2|avx2|x86-64` picks the CPU variant (default `native`, what the machine supports), `--cc clang-22`
   the compiler (default `$CC`, else `clang`; C++ follows it), `--static` links statically, `--benchmarks` also
   builds the micro-benchmarks, `--test` runs the tests, `--clean` starts from scratch (needed to change compiler),
   `--dir` another build directory. Anything after `--` goes to CMake. Each run sets all of these, so nothing is
   left over in the build directory from an earlier run.

       scripts/build-linux-wsl.sh --embed --test
       scripts/build-linux-wsl.sh --cc clang-22 --clean --variant avx2 -- -DTIKI_SANITIZE=address,undefined

-- *build-windows.bat* The same on Windows (LLVM clang targeting the MSVC ABI), into `build-windows\`, with the same
   options apart from `--cc` and `--static` (the CRT is always linked statically). It finds clang in
   `Program Files\LLVM` and CMake and Ninja in Visual Studio when they aren't on the `PATH`. Run it from cmd or
   PowerShell.

       scripts\build-windows.bat --embed --test
       scripts\build-windows.bat --variant x86-64 -- -DTIKI_SANITIZE=address

-- *run_engine_wsl.bat* A Windows batch file that starts the engine (a Linux binary) inside WSL, so it can be
   loaded into Fritz, Cute Chess, etc., or run from a Windows shell for ad-hoc testing. By default it runs the
   CLion release build (`cmake-build-release-wsl-clang`) with the last network in `models\` by name. Set
   `TIKI_APP` or `TIKI_EVALFILE` to override them, as Windows paths (`C:\...`) or WSL paths (`/mnt/c/...`).

-- *run_engine.wsl.example* The same without default paths, copy it to a `.bat` and set `TIKI_APP` and
   `TIKI_EVALFILE`.

-- *run.sh* Starts tiki from a release package with the network shipped in it (see Releases in the main README).
   Not needed for a binary with the network built in (`-DTIKI_EMBED_NETWORK=ON`), which runs on its own.

-- *package.sh* Builds, tests and packages a release variant into `dist/`, used by the release workflow. Extra
   arguments are CMake options, e.g. `scripts/package.sh x86-64-bmi2 -DTIKI_USE_AVX2=ON -DTIKI_USE_PEXT=ON
   -DTIKI_EMBED_NETWORK=ON` to build the newest network in `models/` into the binary.

-- *run.bat* The Windows version of *run.sh*, in the Windows release packages (likewise not needed with the network
   built in).

-- *package_windows.sh* The Windows version of *package.sh* (run from Git Bash), used by the release workflow. It
   takes the same CMake options, including `-DTIKI_EMBED_NETWORK=ON`.

-- *match.sh* / *match.bat* Engine matches with fastchess (downloaded into `tools/` the first time): `sprt <new>
   <base>` tests whether a change makes tiki stronger, `gauntlet <tiki> <engine>...` measures it against other
   engines. Settings (time control, concurrency, games, SPRT bounds, networks) come from the environment, see the
   top of either script. Results go to `matches/`.

-- *nnue_average.py* Averages networks weight by weight into one (Python 3 with numpy), optionally weighted:
   `python3 scripts/nnue_average.py a.nnue b.nnue -o out.nnue --weights 1 3`. It only helps for nets from the same
   training lineage (checkpoints of one run, or fine-tunes of the same parent); independently trained nets don't
   average. SPRT the result against the best of its inputs.
