# tiki

Chess Engine Ansi C

- UCI Compliant
- MoveGen is quite fast (uses PEXT) <2s for Perft-6 of start position. Large number 6K of perft test positions.
- Search is very much _work in progress_ with very basics done, PVS, Null move pruning & Late move reduction.
  - Likely requires some additional work to visualize search tree and spot any sub optimal choices.
  - Transposition table is basic at present and require test cases for UCI, when parsing 'position ... moves ...'
  commands, to ensure hashing is correct (simple move ordering tests in place).
- Time control implemented.
- Eval, just using old NNUE model.
