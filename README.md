# tiki

Chess Engine Ansi C

- UCI Compliant
- MoveGen is quite fast (uses PEXT) <2s for Perft-6 of start position. Large number 6K of perft test positions.
- Search is very much _work in progress_ with very basics done, PVS, Null move pruning & Late move reduction.
  - Likely requires some additional work to visualize search tree and spot any suboptimal choices.
  - Transposition table is basic at present and require test cases for UCI & aging etc.
  - Various refinements to the iterative deepening, LMR etc. would improve strength.
- Time control implemented.
- Eval, using early version of NNUE (and also hand-crafted evaluation) need to expand to some sort of simple NNUE.

- re: ELO a rough estimate just around/above 2000, only tested versus some other engine that claim to be 2000 ELO, it seems to 
handle these fairly easily (using NNUE, will retry with HCE).

- The basic move gen is fast, however rather than generate 'list of moves', should generate list of captures and list
of quiet moves. The sorting can be improved initially by just moving the PV to the top of the list and only sorting the
remaining moves, if necessary.

- Parallel search is an option, even if its just non-deterministically filling the transposition table.
Useful as a prototype for a v2 and test for own NNUE evaluation.