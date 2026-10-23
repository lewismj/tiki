"""HalfKAv2_hm features, the same as engine/core/src/nnue/nnue.c (see nnue.h for the definition).

Squares use tiki's index: a8 = 0, b8 = 1, ... h1 = 63. Pieces use tiki's order P N B R Q K p n b r q k
(0..11). Change both sides together, tests/test_cross_check.py compares them.
"""

KING_BUCKETS = 32
PIECE_KINDS = 11
PIECE_SQUARES = PIECE_KINDS * 64            # 704
INPUTS = KING_BUCKETS * PIECE_SQUARES       # 22528
MAX_ACTIVE = 32

WHITE, BLACK = 0, 1
PIECES = "PNBRQKpnbrqk"
KING = {WHITE: 5, BLACK: 11}


def square(name):
    """'e2' -> tiki square index."""
    return (8 - int(name[1])) * 8 + (ord(name[0]) - ord("a"))


def parse_fen(fen):
    """Returns ([(piece, square), ...], side to move) from a FEN (only the first two fields are used)."""
    fields = fen.split()
    pieces = []
    for rank, row in enumerate(fields[0].split("/")):
        file = 0
        for ch in row:
            if ch.isdigit():
                file += int(ch)
            else:
                pieces.append((PIECES.index(ch), rank * 8 + file))
                file += 1
    side = WHITE if fields[1] == "w" else BLACK
    return pieces, side


def feature_index(perspective, king, piece, sq):
    """Index of 'piece' on 'sq' for 'perspective', whose king is on 'king'."""
    oriented_king = king if perspective == WHITE else king ^ 56
    oriented_sq = sq if perspective == WHITE else sq ^ 56
    if (oriented_king & 7) >= 4:                # Horizontal mirror, king on files a-d.
        oriented_king ^= 7
        oriented_sq ^= 7
    bucket = (oriented_king >> 3) * 4 + (oriented_king & 7)

    kind_type = piece % 6
    colour = BLACK if piece >= 6 else WHITE
    kind = 10 if kind_type == 5 else kind_type * 2 + (colour != perspective)
    return bucket * PIECE_SQUARES + kind * 64 + oriented_sq


def active_features(pieces, perspective):
    king = next(sq for pc, sq in pieces if pc == KING[perspective])
    return [feature_index(perspective, king, pc, sq) for pc, sq in pieces]
