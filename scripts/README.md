### Scripts

-- *run_engine_wsl.bat* A Windows batch file that starts the engine (a Linux binary) inside WSL, so it can be
   loaded into Fritz, Cute Chess, etc., or run from a Windows shell for ad-hoc testing. By default it runs the
   CLion release build (`cmake-build-release-wsl-clang`) with the last network in `models\` by name. Set
   `TIKI_APP` or `TIKI_EVALFILE` to override them, as Windows paths (`C:\...`) or WSL paths (`/mnt/c/...`).

-- *run_engine.wsl.example* The same without default paths, copy it to a `.bat` and set `TIKI_APP` and
   `TIKI_EVALFILE`.

-- *run.sh* Starts tiki from a release package with the network shipped in it (see Releases in the main README).

-- *package.sh* Builds, tests and packages a release variant into `dist/`, used by the release workflow.

-- *run.bat* The Windows version of *run.sh*, in the Windows release packages.

-- *package_windows.sh* The Windows version of *package.sh* (run from Git Bash), used by the release workflow.
