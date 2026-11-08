"""HalfKAv2_hm features, the same as engine/core/src/nnue/nnue.c (see nnue.h for the definition).

Squares use tiki's index: a8 = 0, b8 = 1, ... h1 = 63. Pieces use tiki's order P N B R Q K p n b r q k
(0..11). Change both sides together, tests/test_cross_check.py compares them.
"""

KING_BUCKETS = 32
PIECE_KINDS = 11
PIECE_SQUARES = PIECE_KINDS * 64            # 704
INPUTS = KING_BUCKETS * PIECE_SQUARES       # 22528
MAX_ACTIVE = 32
PIECE_BUCKETS = 8                           # By the number of pieces (piece_bucket).
OUTPUT_BUCKETS = PIECE_BUCKETS * 4          # x which sides have a queen (output_bucket): 32 layer stacks.
OWN_QUEEN_KIND, THEIR_QUEEN_KIND = 8, 9     # Feature kinds (feature_index) of the perspective's queens and the other side's.

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


def piece_bucket(piece_count):
    """The piece count part of the output bucket (kings included): (n - 2) / 4, so 2-5 pieces is 0 ...
    30-32 is 7. Works on ints and integer tensors alike."""
    return (piece_count - 2) // 4


def output_bucket(piece_count, stm_queen, nstm_queen):
    """The layer stack a position uses (as Stockfish's SFNNv18): piece_bucket * 4, plus 2 if the side to
    move has a queen and 1 if the other side has. 0..31. Works on ints / bools and tensors alike."""
    return piece_bucket(piece_count) * 4 + stm_queen * 2 + nstm_queen * 1


def bucket_of_features(stm_features):
    """output_bucket from the side to move's active features (one per piece, so their count is the
    piece count, and the queens are kinds OWN_QUEEN_KIND / THEIR_QUEEN_KIND)."""
    kinds = [feature % PIECE_SQUARES // 64 for feature in stm_features]
    return output_bucket(len(stm_features), int(OWN_QUEEN_KIND in kinds), int(THEIR_QUEEN_KIND in kinds))


def position_bucket(pieces, side):
    """output_bucket of parse_fen's (pieces, side)."""
    queens = {piece for piece, _ in pieces if piece % 6 == 4}       # 4 = Q, 10 = q.
    stm_queen = (4 if side == WHITE else 10) in queens
    nstm_queen = (10 if side == WHITE else 4) in queens
    return output_bucket(len(pieces), int(stm_queen), int(nstm_queen))


def active_features(pieces, perspective):
    king = next(sq for pc, sq in pieces if pc == KING[perspective])
    return [feature_index(perspective, king, pc, sq) for pc, sq in pieces]
