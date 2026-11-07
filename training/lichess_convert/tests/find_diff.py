"""Finds the first random game (compare.py's generator) where lichess_convert and lichess.py differ."""
import os, random, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare
import lichess
tool, seed, rnd, skip, elo = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
rng = random.Random(seed * 1000 + rnd)
games = [compare.random_game(rng, i) for i in range(2000)]
conv = lichess.Converter(skip, 1.0, elo, 3000, 1, True)
with tempfile.TemporaryDirectory() as tmp:
    for i, g in enumerate(games):
        py = conv(g).tobytes() if "[%eval " in g else b""
        path = os.path.join(tmp, "g.pgn"); out = os.path.join(tmp, "g.bin")
        open(path, "w").write(g)
        subprocess.run([tool, "--skip-plies", str(skip), "--min-elo", str(elo), "--threads", "1", path, out], capture_output=True)
        c = open(out, "rb").read()[8:]
        if c != py:
            print(f"game {i}: python {len(py)//68} records, C {len(c)//68}")
            print(g)
            for j in range(0, max(len(py), len(c)), 68):
                if py[j:j+68] != c[j:j+68]:
                    print("first differing record", j // 68); break
            break
