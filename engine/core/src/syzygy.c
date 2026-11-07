#include <string.h>

#include "syzygy.h"
#include "move_encoding.h"
#include "move_generator.h"
#include "tbprobe.h"

int syzygy_probe_depth = SYZYGY_PROBE_DEPTH_DEFAULT;
int syzygy_search_pieces = 0;

/* The root's moves the search may play (syzygy_rank_root), when 'root_restricted'. */
static move_t root_moves[MOVE_BUFFER_SIZE];
static int root_move_count = 0;
static bool root_restricted = false;
static bool root_in_tables = false;
static int root_wdl = 0;

/* Fathom's root results, too large for the stack (about 100 KB). Only the main thread uses them. */
static struct TbRootMoves tb_root_moves;

int syzygy_init(const char* path) {
    syzygy_end_search();
    /* Fathom unloads the previous tables first, and loads none for "" or "<empty>". */
    if (!tb_init(path == NULL ? "" : path)) TB_LARGEST = 0;
    syzygy_search_pieces = (int) TB_LARGEST;
    return (int) TB_LARGEST;
}

int syzygy_largest(void) {
    return (int) TB_LARGEST;
}

/* Fathom's description of 'board' (bitboards from a1), see syzygy_bitboard. */
typedef struct {
    uint64_t white, black, kings, queens, rooks, bishops, knights, pawns;
    unsigned ep;
    bool turn;
} tb_position_t;

static tb_position_t tb_position(const board_t* board) {
    const bitboard* pc = board->pieces;
    return (tb_position_t) {
        .white = syzygy_bitboard(board->occupancy[white]),
        .black = syzygy_bitboard(board->occupancy[black]),
        .kings = syzygy_bitboard(pc[K] | pc[k]),
        .queens = syzygy_bitboard(pc[Q] | pc[q]),
        .rooks = syzygy_bitboard(pc[R] | pc[r]),
        .bishops = syzygy_bitboard(pc[B] | pc[b]),
        .knights = syzygy_bitboard(pc[N] | pc[n]),
        .pawns = syzygy_bitboard(pc[P] | pc[p]),
        .ep = board->en_passant == none_sq ? 0 : (unsigned) (board->en_passant ^ 56),
        .turn = board->side == white,
    };
}

bool syzygy_probe_wdl(const board_t* board, int* wdl) {
    if (pop_count(board->occupancy[both]) > (int) TB_LARGEST) return false;
    const tb_position_t pos = tb_position(board);
    const unsigned result = tb_probe_wdl(pos.white, pos.black, pos.kings, pos.queens, pos.rooks, pos.bishops,
                                         pos.knights, pos.pawns, (unsigned) board->fifty_move,
                                         (unsigned) board->castle_flag, pos.ep, pos.turn);
    if (result == TB_RESULT_FAILED) return false;
    /* Cursed wins and blessed losses are draws under the fifty-move rule. */
    *wdl = result == TB_WIN ? 1 : result == TB_LOSS ? -1 : 0;
    return true;
}

/* Fathom's promotion code of a tiki move (TB_PROMOTES_*). */
static unsigned tb_promotes(move_t mv) {
    const piece promoted = get_promoted_piece(mv);
    if (!promoted) return TB_PROMOTES_NONE;
    switch (promoted % 6) {
        case Q: return TB_PROMOTES_QUEEN;
        case R: return TB_PROMOTES_ROOK;
        case B: return TB_PROMOTES_BISHOP;
        default: return TB_PROMOTES_KNIGHT;
    }
}

/* Whether Fathom's move 'tb' is tiki's 'mv'. */
static bool same_move(TbMove tb, move_t mv) {
    return TB_MOVE_FROM(tb) == (unsigned) (get_source_square(mv) ^ 56) &&
           TB_MOVE_TO(tb) == (unsigned) (get_target_square(mv) ^ 56) &&
           TB_MOVE_PROMOTES(tb) == tb_promotes(mv);
}

bool syzygy_rank_root(board_t* board) {
    syzygy_end_search();
    if (TB_LARGEST == 0 || board->castle_flag != 0 || pop_count(board->occupancy[both]) > (int) TB_LARGEST)
        return false;

    const tb_position_t pos = tb_position(board);
    /* DTZ keeps a win within the fifty-move rule; WDL if a DTZ table is missing (or none were given). */
    bool dtz = tb_probe_root_dtz(pos.white, pos.black, pos.kings, pos.queens, pos.rooks, pos.bishops, pos.knights,
                                 pos.pawns, (unsigned) board->fifty_move, 0, pos.ep, pos.turn, false, true,
                                 &tb_root_moves) != 0;
    if (!dtz && tb_probe_root_wdl(pos.white, pos.black, pos.kings, pos.queens, pos.rooks, pos.bishops, pos.knights,
                                  pos.pawns, (unsigned) board->fifty_move, 0, pos.ep, pos.turn, true,
                                  &tb_root_moves) == 0)
        return false;
    if (tb_root_moves.size == 0) return false;  /* Mate or stalemate: the search scores it. */

    int best_rank = tb_root_moves.moves[0].tbRank;
    int best_score = tb_root_moves.moves[0].tbScore;
    for (unsigned i = 1; i < tb_root_moves.size; i++) {
        if (tb_root_moves.moves[i].tbRank > best_rank) {
            best_rank = tb_root_moves.moves[i].tbRank;
            best_score = tb_root_moves.moves[i].tbScore;
        }
    }

    /* tiki's legal moves with the best rank. */
    move_buffer_t buffer;
    generate_moves(board, &buffer);
    root_move_count = 0;
    for (int i = 0; i < buffer.index; i++) {
        const move_t mv = buffer.moves[i];
        const bool legal = make_move(board, mv);
        pop_move(board);
        if (!legal) continue;
        for (unsigned j = 0; j < tb_root_moves.size; j++) {
            if (tb_root_moves.moves[j].tbRank == best_rank && same_move(tb_root_moves.moves[j].move, mv)) {
                root_moves[root_move_count++] = mv;
                break;
            }
        }
    }
    if (root_move_count == 0) return false;  /* Can't happen, the two move generators agree. */

    root_restricted = true;
    root_in_tables = true;
    /* Fathom's scores: wins and losses beyond a pawn's value, draws (including cursed ones) within it. */
    root_wdl = best_score > TB_VALUE_PAWN ? 1 : best_score < -TB_VALUE_PAWN ? -1 : 0;
    /* With DTZ, every move left keeps the result, so the search probes nothing and finds the way by
     * itself. With WDL only, it keeps probing when winning, as nothing else steers it to the win. */
    syzygy_search_pieces = dtz || root_wdl <= 0 ? 0 : (int) TB_LARGEST;
    return true;
}

bool syzygy_root_allows(move_t mv) {
    if (!root_restricted) return true;
    for (int i = 0; i < root_move_count; i++) {
        if (root_moves[i] == mv) return true;
    }
    return false;
}

bool syzygy_root_wdl(int* wdl) {
    if (root_in_tables) *wdl = root_wdl;
    return root_in_tables;
}

void syzygy_end_search(void) {
    root_restricted = false;
    root_in_tables = false;
    root_move_count = 0;
    syzygy_search_pieces = (int) TB_LARGEST;
}
