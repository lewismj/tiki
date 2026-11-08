# Fathom

Syzygy tablebase probing, from [Fathom](https://github.com/jdart1/Fathom) by Ronald de Man, basil00 and Jon Dart,
under the MIT licence (`LICENSE`, which must stay with these files).

- **Commit:** `c9c6fef0dddc05d2e242c183acf5833149ab676d`
- **Files:** `src/tbprobe.c`, `tbprobe.h`, `tbchess.c` (included by `tbprobe.c`), `tbconfig.h` and `stdendian.h`,
  unchanged.

tiki uses it through `engine/core/src/syzygy.c`. To update, replace these files with those of a newer commit,
change the commit above, and run the Syzygy tests (`TIKI_SYZYGY_PATH=<tables> ctest`).
`training/syzygy_relabel` downloads the same commit at build time.
