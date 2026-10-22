## tiki

Chess Engine Ansi C

### Core Features

- UCI Compliant

- **MoveGen** is quite fast (uses PEXT) <2s for Perft-6 of start position. Large number (6K) of perft test positions.

- **Search** is _work in progress_ with very basics done, PVS, Null move pruning & Late move reduction.
  - Transposition table is basic at present and require test cases for UCI & aging etc.
  - Various refinements to the iterative deepening, LMR etc. would improve strength.
  
- **Time control** implemented.

- **Eval**, using early version of NNUE (and also hand-crafted evaluation) need to expand to some sort of simple NNUE.

- re: ELO a rough estimate just around/above 2000, only tested versus some other engine that claim to be 2000 ELO, it seems to 
handle these fairly easily, but need to run via profiler to gauge search hostspot.

### Building and testing

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && ctest -LE slow        # unit tests and bench smoke test; 'ctest' also runs the full perft file
```

- `-DTIKI_USE_PEXT=OFF` uses magic bitboards (default is on when the build machine has BMI2; turn it off on AMD Zen 1/2).
- `-DTIKI_SANITIZE=address,undefined` (or `thread`) builds everything with sanitizers.
- `-DTIKI_BUILD_BENCHMARKS=OFF` skips fetching Google Benchmark.

### Benchmarking

- `tiki_app bench [depth]` (or `bench [depth]` in the UCI loop) searches 16 fixed positions to a fixed depth
  (default 11) with a 16MB TT cleared before each one, and prints the total node count and nodes/second. The node count
  only changes when the search or evaluation does, so it's a signature for refactors that shouldn't change behaviour.
- `build/engine/benchmark/tiki_benchmark` runs Google Benchmark micro-benchmarks of evaluation, move generation,
  make/undo, perft and a fixed-depth search, for comparing an evaluation change on its own.

### Notes

- The basic move gen is fast, however rather than generate 'list of moves', should generate list of captures and list
of quiet moves. The sorting can be improved initially by just moving the PV to the top of the list and only sorting the
remaining moves, if necessary.

- Parallel search is an option, even if its just non-deterministically filling the transposition table.
Useful as a prototype for a v2 and test for own NNUE evaluation.