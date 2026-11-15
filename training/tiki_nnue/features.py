"""HalfKAv2_hm features plus PawnPairs, the same as engine/core/src/nnue/nnue.c (see nnue.h for the definition).

Squares use tiki's index: a8 = 0, b8 = 1, ... h1 = 63. Pieces use tiki's order P N B R Q K p n b r q k
(0..11). Change both sides together, tests/test_cross_check.py compares them.

PawnPairs (Stockfish's PP_3Wide): one feature per unordered pair of pawns, of either colour, on the same or
adjacent files, from each perspective: the squares oriented and mirrored as the piece features' are, each
pawn's id = (theirs) * 48 + oriented square - 8 (ranks 2-7, 0..95), and the pair's index = hi * (hi - 1) / 2 + lo
after INPUTS. No king buckets.
"""

KING_BUCKETS = 32
PIECE_KINDS = 11
PIECE_SQUARES = PIECE_KINDS * 64            # 704
INPUTS = KING_BUCKETS * PIECE_SQUARES       # 22528, the piece features
PAWN_IDS = 2 * 48                           # Own and their pawns on ranks 2-7.
PAIRS = PAWN_IDS * (PAWN_IDS - 1) // 2      # 4560
TOTAL_INPUTS = INPUTS + PAIRS               # 27088 rows of the feature transformer
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


def orient(perspective, king, sq):
    """(oriented king, oriented sq): rank flipped for black, both mirrored when the king is on files e-h."""
    oriented_king = king if perspective == WHITE else king ^ 56
    oriented_sq = sq if perspective == WHITE else sq ^ 56
    if (oriented_king & 7) >= 4:                # Horizontal mirror, king on files a-d.
        oriented_king ^= 7
        oriented_sq ^= 7
    return oriented_king, oriented_sq


def feature_index(perspective, king, piece, sq):
    """Index of 'piece' on 'sq' for 'perspective', whose king is on 'king'."""
    oriented_king, oriented_sq = orient(perspective, king, sq)
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
    """output_bucket from the side to move's active features (one piece feature per piece, so their count is
    the piece count, and the queens are kinds OWN_QUEEN_KIND / THEIR_QUEEN_KIND; pair features are ignored)."""
    kinds = [feature % PIECE_SQUARES // 64 for feature in stm_features if feature < INPUTS]
    return output_bucket(len(kinds), int(OWN_QUEEN_KIND in kinds), int(THEIR_QUEEN_KIND in kinds))


def position_bucket(pieces, side):
    """output_bucket of parse_fen's (pieces, side)."""
    queens = {piece for piece, _ in pieces if piece % 6 == 4}       # 4 = Q, 10 = q.
    stm_queen = (4 if side == WHITE else 10) in queens
    nstm_queen = (10 if side == WHITE else 4) in queens
    return output_bucket(len(pieces), int(stm_queen), int(nstm_queen))


def active_features(pieces, perspective):
    """The piece features of parse_fen's 'pieces' for 'perspective' (one per piece)."""
    king = next(sq for pc, sq in pieces if pc == KING[perspective])
    return [feature_index(perspective, king, pc, sq) for pc, sq in pieces]


def pawn_id(perspective, king, piece, sq):
    """A pawn's id in PawnPairs for 'perspective' (king on 'king'): (theirs) * 48 + oriented sq - 8, 0..95."""
    _, oriented_sq = orient(perspective, king, sq)
    theirs = int((piece >= 6) != (perspective == BLACK))
    return theirs * 48 + oriented_sq - 8


def pair_index(id_a, id_b):
    """The feature of the pawn pair with ids 'id_a' and 'id_b' (different), after the piece features."""
    hi, lo = max(id_a, id_b), min(id_a, id_b)
    return INPUTS + hi * (hi - 1) // 2 + lo


def active_pair_features(pieces, perspective):
    """The PawnPairs features of parse_fen's 'pieces' for 'perspective': every pair of pawns, either colour,
    whose files differ by at most one."""
    king = next(sq for pc, sq in pieces if pc == KING[perspective])
    pawns = [(pc, sq) for pc, sq in pieces if pc % 6 == 0]
    features = []
    for i, (pc_a, sq_a) in enumerate(pawns):
        for pc_b, sq_b in pawns[i + 1:]:
            if abs((sq_a & 7) - (sq_b & 7)) <= 1:
                features.append(pair_index(pawn_id(perspective, king, pc_a, sq_a), pawn_id(perspective, king, pc_b, sq_b)))
    return features


def all_features(pieces, perspective):
    """Every active feature for 'perspective': the pieces', then the pawn pairs'."""
    return active_features(pieces, perspective) + active_pair_features(pieces, perspective)
