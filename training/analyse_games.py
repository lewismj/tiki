#!/usr/bin/env python3
"""Why tiki lost (or failed to win) the games of a match: each game's turning point, by phase and cause.

    python3 analyse_games.py --pgn games.pgn --referee stockfish --tiki tiki --out DIR
                             [--opponent leorik] [--name tiki] [--scope missed|losses|all]
                             [--scan-time 0.1] [--check-time 2] [--search-time 10] [--workers 3] [--limit N]

For each game examined, the referee (Stockfish, say) scores every position at --scan-time, as tiki's expected
score (Stockfish's WDL model). The turning point is tiki's move that dropped it the most. Games examined
(--scope):
    losses   the games tiki lost
    missed   (default) those, and the games it didn't win after the referee had it winning (expected score
             at least 0.8 at some point): their turning point is tiki's costliest move after that
    all      every game
Each turning point is classified by kind (loss, missed win, or draw/win with --scope all), by phase
    opening      move 15 or earlier
    endgame      at most 4 pieces other than kings and pawns, before the move
    middlegame   otherwise
and by cause: a drop under 0.15 has no clear turning point ('gradual'); otherwise tiki searches the position
(the game's moves to it, so repetitions count) afresh for --search-time, the referee scores its choice for
--check-time, and if that recovers at least half the drop more depth would have found it ('depth'), if not
it's the evaluation ('eval'). With --opponent, the competitor searches the same position for the same time,
scored the same way: a turning point it solves and tiki doesn't is a concrete gap.

Writes positions.csv (one row per game examined) and summary.json to --out after every game, so a partial
run can be read. Engines run through python-chess (pip install chess), one game per worker.
"""

import argparse
import csv
import io
import json
import multiprocessing
import os
import re
import sys
import time

import chess
import chess.engine
import chess.pgn

WINNING = 0.8           # Expected score from which a game counts as won, for missed wins.
CLEAR_DROP = 0.15       # Smaller drops have no clear turning point.
SOLVED = 0.5            # Fraction of the drop a move must recover to solve the turning point.
PHASES = ("opening", "middlegame", "endgame")
CAUSES = ("depth", "eval", "gradual")
KINDS = ("loss", "missed win", "draw", "win")
DEPTH_RE = re.compile(r"\s*([+-]?[\d.]+|[+-]?M\d+)/(\d+)")


# ---- Engine-free parts (tests/test_analyse_games.py) ----

def tiki_colour(headers, name):
    """tiki's colour in a game, None if it didn't play."""
    if headers.get("White") == name:
        return chess.WHITE
    if headers.get("Black") == name:
        return chess.BLACK
    return None


def outcome(result, colour):
    """'win', 'draw', 'loss' for tiki, None for an unfinished game."""
    if result == "1/2-1/2":
        return "draw"
    if result not in ("1-0", "0-1"):
        return None
    return "win" if (result == "1-0") == (colour == chess.WHITE) else "loss"


def candidate(result, scope):
    """Whether a game with this outcome for tiki is scanned at all under 'scope'."""
    if result is None:
        return False
    if scope == "all":
        return True
    if scope == "losses":
        return result == "loss"
    return result != "win"


def phase(board):
    if board.fullmove_number <= 15:
        return "opening"
    pieces = sum(len(board.pieces(kind, colour)) for kind in (chess.KNIGHT, chess.BISHOP, chess.ROOK, chess.QUEEN)
                 for colour in (chess.WHITE, chess.BLACK))
    return "endgame" if pieces <= 4 else "middlegame"


def turning_point(expected, tiki_plies, result, scope):
    """The game's kind and turning point, from tiki's expected score after each ply ('expected', from the start
    position: one longer than the game) and the plies tiki moved at. (kind, ply of tiki's move) or None if
    the game isn't examined: a draw under 'missed' that was never winning. The turning point is tiki's move
    that dropped the expected score the most; for a missed win, the most after it was first winning."""
    peak = next((ply for ply, value in enumerate(expected) if value >= WINNING), None)
    if result == "win":
        kind = "win"
    elif peak is not None:
        kind = "missed win"
    elif result == "loss":
        kind = "loss"
    elif scope == "all":
        kind = "draw"
    else:
        return None
    first = peak if kind == "missed win" else 0
    plies = [ply for ply in tiki_plies if ply >= first and ply + 1 < len(expected)]
    if not plies:
        return None
    return kind, max(plies, key=lambda ply: expected[ply] - expected[ply + 1])


def cause(drop, recovered):
    if drop < CLEAR_DROP:
        return "gradual"
    return "depth" if recovered >= SOLVED else "eval"


def game_depth(comment):
    """tiki's depth for a move, from fastchess's comment ('+0.35/14 0.21s'), -1 if it doesn't say."""
    match = DEPTH_RE.match(comment or "")
    return int(match[2]) if match else -1


def summarise(rows, meta):
    """summary.json: the counts by kind, phase and cause, what the opponent solved, the costliest turning points."""
    counts = {kind: {p: {c: 0 for c in CAUSES} for p in PHASES} for kind in KINDS}
    for row in rows:
        counts[row["kind"]][row["phase"]][row["cause"]] += 1
    opponent = {c: {"tried": 0, "solved": 0} for c in ("depth", "eval")}
    for row in rows:
        if row["cause"] in opponent and row["opponent_move"]:
            opponent[row["cause"]]["tried"] += 1
            opponent[row["cause"]]["solved"] += row["opponent_solves"] == "yes"
    top = sorted((row for row in rows if row["cause"] != "gradual"), key=lambda row: -row["drop"])[:10]
    return dict(meta, examined=len(rows), counts=counts, opponent=opponent, top=top)


# ---- The engines ----

def expectation(info, colour):
    return info["score"].pov(colour).wdl(model="sf").expectation()


def open_engine(path, hash_mb):
    engine = chess.engine.SimpleEngine.popen_uci(path, timeout=60)
    options = {"Hash": hash_mb}
    engine.configure({k: v for k, v in options.items() if k in engine.options})
    return engine


def recovered_by(referee, board, move, colour, after, drop, seconds):
    """The fraction of 'drop' that playing 'move' instead recovers, scored by the referee (0 for the game's move)."""
    child = board.copy()
    child.push(move)
    return (expectation(referee.analyse(child, chess.engine.Limit(time=seconds)), colour) - after) / drop


def analyse_game(job):
    index, pgn_text, options = job
    game = chess.pgn.read_game(io.StringIO(pgn_text))
    colour = tiki_colour(game.headers, options["name"])
    result = outcome(game.headers.get("Result"), colour)
    referee = open_engine(options["referee"], 128)
    try:
        board = game.board()
        scan = chess.engine.Limit(time=options["scan_time"])
        expected = [expectation(referee.analyse(board, scan), colour)]
        nodes, tiki_plies = list(game.mainline()), []
        for ply, node in enumerate(nodes):
            if board.turn == colour:
                tiki_plies.append(ply)
            board.push(node.move)
            expected.append(expectation(referee.analyse(board, scan), colour))
        found = turning_point(expected, tiki_plies, result, options["scope"])
        if found is None:
            return index, None
        kind, ply = found
        node = nodes[ply]
        before = game.board()
        for earlier in nodes[:ply]:
            before.push(earlier.move)
        drop, after = expected[ply] - expected[ply + 1], expected[ply + 1]
        row = {"game": index, "round": game.headers.get("Round", ""), "opponent": game.headers.get(
                   "Black" if colour == chess.WHITE else "White", ""),
               "tiki": "white" if colour == chess.WHITE else "black", "result": result, "kind": kind,
               "move": f"{before.fullmove_number}{'.' if colour == chess.WHITE else '...'}{before.san(node.move)}",
               "drop": round(drop, 3), "before": round(expected[ply], 3), "after": round(after, 3),
               "peak": round(max(expected), 3), "game_depth": game_depth(node.comment), "phase": phase(before),
               "cause": "gradual", "tiki_move": "", "recovered": "", "opponent_move": "", "opponent_recovered": "",
               "opponent_solves": "", "fen": before.fen()}
        if drop >= CLEAR_DROP:
            search = chess.engine.Limit(time=options["search_time"])
            tiki = open_engine(options["tiki"], 256)
            try:
                choice = tiki.analyse(before, search)["pv"][0]
            finally:
                tiki.quit()
            recovered = 0.0 if choice == node.move else recovered_by(referee, before, choice, colour, after, drop,
                                                                     options["check_time"])
            row.update(cause=cause(drop, recovered), tiki_move=before.san(choice), recovered=round(recovered, 2))
            if options["opponent"]:
                opponent = open_engine(options["opponent"], 256)
                try:
                    reply = opponent.analyse(before, search)["pv"][0]
                finally:
                    opponent.quit()
                solved = 0.0 if reply == node.move else recovered_by(referee, before, reply, colour, after, drop,
                                                                     options["check_time"])
                row.update(opponent_move=before.san(reply), opponent_recovered=round(solved, 2),
                           opponent_solves="yes" if solved >= SOLVED else "no")
        return index, row
    finally:
        referee.quit()


# ---- The run ----

def read_games(path, name, scope, limit):
    """The games to scan (index, PGN text), tiki's score over every game it played in the file, its opponents."""
    jobs, score, opponents = [], {"win": 0, "draw": 0, "loss": 0}, set()
    with open(path, encoding="utf-8", errors="replace") as handle:
        index = 0
        while (game := chess.pgn.read_game(handle)) is not None:
            index += 1
            colour = tiki_colour(game.headers, name)
            if colour is None:
                continue
            result = outcome(game.headers.get("Result"), colour)
            if result is not None:
                score[result] += 1
            opponents.add(game.headers.get("Black" if colour == chess.WHITE else "White", "?"))
            if candidate(result, scope) and (limit is None or len(jobs) < limit):
                jobs.append((index, str(game)))
    return jobs, score, sorted(opponents)


def write(out, rows, meta):
    rows = sorted(rows, key=lambda row: row["game"])
    if rows:
        with open(os.path.join(out, "positions.csv.tmp"), "w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        os.replace(os.path.join(out, "positions.csv.tmp"), os.path.join(out, "positions.csv"))
    with open(os.path.join(out, "summary.json.tmp"), "w", encoding="utf-8") as handle:
        json.dump(summarise(rows, meta), handle, indent=1)
    os.replace(os.path.join(out, "summary.json.tmp"), os.path.join(out, "summary.json"))


def print_tally(rows):
    print("\nTurning points (phase x cause):")
    print(f"{'':12}" + "".join(f"{c:>9}" for c in CAUSES) + f"{'total':>9}")
    for p in PHASES:
        counts = [sum(1 for row in rows if row["phase"] == p and row["cause"] == c) for c in CAUSES]
        print(f"{p:12}" + "".join(f"{n:>9}" for n in counts) + f"{sum(counts):>9}")
    counts = [sum(1 for row in rows if row["cause"] == c) for c in CAUSES]
    print(f"{'total':12}" + "".join(f"{n:>9}" for n in counts) + f"{len(rows):>9}")
    for kind in KINDS:
        if n := sum(1 for row in rows if row["kind"] == kind):
            print(f"{kind}: {n}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--pgn", required=True)
    parser.add_argument("--referee", required=True, help="the engine that judges the moves (Stockfish)")
    parser.add_argument("--tiki", required=True, help="the tiki build that searches the turning points")
    parser.add_argument("--opponent", default="", help="the competitor, to search the turning points too")
    parser.add_argument("--out", required=True)
    parser.add_argument("--name", default="tiki", help="tiki's name in the PGN")
    parser.add_argument("--scope", choices=("missed", "losses", "all"), default="missed")
    parser.add_argument("--scan-time", type=float, default=0.1)
    parser.add_argument("--check-time", type=float, default=2.0)
    parser.add_argument("--search-time", type=float, default=10.0)
    parser.add_argument("--workers", type=int, default=3)
    parser.add_argument("--limit", type=int, default=None, help="scan at most this many games")
    args = parser.parse_args(argv)

    os.makedirs(args.out, exist_ok=True)
    jobs, score, opponents = read_games(args.pgn, args.name, args.scope, args.limit)
    meta = {"pgn": os.path.abspath(args.pgn), "name": args.name, "tiki_engine": args.tiki, "referee": args.referee,
            "opponent_engine": args.opponent, "opponents": opponents, "scope": args.scope, "scan_time": args.scan_time,
            "check_time": args.check_time, "search_time": args.search_time, "score": score,
            "games": sum(score.values()), "scanned": 0, "to_scan": len(jobs),
            "started": time.strftime("%Y-%m-%d %H:%M:%S"), "finished": None}
    if not score["win"] + score["draw"] + score["loss"]:
        sys.exit(f"No finished games of '{args.name}' in {args.pgn} (is --name right?)")
    print(f"{meta['games']} games of {args.name} (+{score['win']} ={score['draw']} -{score['loss']}), "
          f"{len(jobs)} to scan ({args.scope})", flush=True)

    options = dict(name=args.name, referee=args.referee, tiki=args.tiki, opponent=args.opponent,
                   scope=args.scope, scan_time=args.scan_time, check_time=args.check_time,
                   search_time=args.search_time)
    rows = []
    write(args.out, rows, meta)
    with multiprocessing.Pool(max(1, args.workers)) as pool:
        for done, (index, row) in enumerate(pool.imap_unordered(analyse_game, [job + (options,) for job in jobs]), 1):
            meta["scanned"] = done
            percent = 100 * done / len(jobs)
            if row is None:
                print(f"[{done}/{len(jobs)}] {percent:.1f}% game {index}: not examined (never winning)", flush=True)
            else:
                rows.append(row)
                print(f"[{done}/{len(jobs)}] {percent:.1f}% game {index} {row['kind']} {row['tiki']} {row['move']} "
                      f"drop {row['drop']} {row['phase']} {row['cause']} (game d{row['game_depth']}, "
                      f"tiki: {row['tiki_move'] or '-'}, opponent: {row['opponent_move'] or '-'})", flush=True)
            write(args.out, rows, meta)
    meta["finished"] = time.strftime("%Y-%m-%d %H:%M:%S")
    write(args.out, rows, meta)
    print_tally(rows)
    print(f"\nWritten to {args.out}")


if __name__ == "__main__":
    main()
