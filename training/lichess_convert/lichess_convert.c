/*
 * lichess_convert: a Lichess dump (.pgn.zst, or plain .pgn) to the trainer's binary data, as lichess.py does
 * (tiki_nnue/binary.py's format), many times faster. With --sample 1.0 (keep every position) the output is
 * byte for byte lichess.py's. lichess.py reads games with python-chess, so the PGN reading here follows
 * python-chess's rules (chess/pgn.py read_game and GameBuilder, Board.parse_san, GameNode.eval), quirks included.
 *
 *     lichess_convert [options] <dump.pgn.zst | dump.pgn> <out.bin>
 *
 *     --min-elo N       skip games where either player is rated lower (default 0: no filter)
 *     --skip-plies N    opening plies to skip (default 16)
 *     --max-score N     skip positions scored above this, centipawns (default 3000)
 *     --sample F        fraction of positions to keep (default 1.0). Below 1.0 the choice is deterministic
 *                       but NOT lichess.py's (which uses blake2b and Python's Mersenne Twister)
 *     --seed N          sampling seed (default 1)
 *     --max-positions N stop after the game that reaches this many (default 0: all)
 *     --threads N       worker threads (default: the CPUs less one)
 *
 * Positions kept, as lichess.py: mainline positions after ply 'skip-plies' with a Lichess '[%eval]' that isn't
 * a mate and is within 'max-score', not in check, and whose next move is neither a capture nor a promotion.
 * Records: the pieces in square order (a8 first, tiki's index) as piece * 64 + square, padded with 0xFFFF to 32,
 * the score and result for the side to move, and the side.
 *
 * The output goes to <out.bin>.part, renamed when complete. One thread decompresses (libzstd.so.1, no headers
 * needed) and cuts the text into chunks of whole games; the workers convert the chunks; they're written in order.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "board.h"
#include "move_generator.h"
#include "attack_mask.h"
#include "zobrist_key.h"

/* ---- libzstd's streaming decompression (zstd.h, stable since 1.0), declared here when the headers
 *      (libzstd-dev) aren't installed. ---- */
#if __has_include(<zstd.h>)
#include <zstd.h>
#else
typedef struct ZSTD_DCtx_s ZSTD_DStream;
typedef struct { const void* src; size_t size; size_t pos; } ZSTD_inBuffer;
typedef struct { void* dst; size_t size; size_t pos; } ZSTD_outBuffer;
ZSTD_DStream* ZSTD_createDStream(void);
size_t ZSTD_freeDStream(ZSTD_DStream* zds);
size_t ZSTD_initDStream(ZSTD_DStream* zds);
size_t ZSTD_decompressStream(ZSTD_DStream* zds, ZSTD_outBuffer* output, ZSTD_inBuffer* input);
unsigned ZSTD_isError(size_t code);
const char* ZSTD_getErrorName(size_t code);
#endif

/* ---- Options ---- */
static int opt_min_elo = 0;
static int opt_skip_plies = 16;
static int opt_max_score = 3000;
static double opt_sample = 1.0;
static uint64_t opt_seed = 1;
static uint64_t opt_max_positions = 0;
static int opt_threads = 0;

static void* xrealloc(void* p, size_t n) {
    p = realloc(p, n);
    if (!p) { fprintf(stderr, "lichess_convert: out of memory\n"); exit(1); }
    return p;
}

/* ---- Output records (binary.py RECORD, 68 bytes, little-endian, packed) ---- */
#define MAX_ACTIVE 32
#define PAD 0xFFFF
#pragma pack(push, 1)
typedef struct {
    uint16_t pieces[MAX_ACTIVE];
    int16_t score;
    uint8_t result;
    uint8_t side;
} record_t;
#pragma pack(pop)
_Static_assert(sizeof(record_t) == 68, "binary.py RECORD is 68 bytes");
static const char MAGIC[8] = { 'T', 'I', 'K', 'I', 'B', 'I', 'N', '1' };

typedef struct {
    record_t* records;
    size_t count, capacity;
    uint64_t* game_ends;        /* Records after each evaluated game (lichess.py's 'games'), for --max-positions. */
    size_t games, games_capacity;
} output_t;

static void push_record(output_t* out, const record_t* rec) {
    if (out->count == out->capacity) {
        out->capacity = out->capacity ? out->capacity * 2 : 1 << 16;
        out->records = xrealloc(out->records, out->capacity * sizeof(record_t));
    }
    out->records[out->count++] = *rec;
}

static void end_game(output_t* out) {
    if (out->games == out->games_capacity) {
        out->games_capacity = out->games_capacity ? out->games_capacity * 2 : 1 << 14;
        out->game_ends = xrealloc(out->game_ends, out->games_capacity * sizeof(uint64_t));
    }
    out->game_ends[out->games++] = out->count;
}

/* ---- Positions. A position is the board without its undo stack (POS_BYTES); moves keep no undo information,
 *      so games of any length work. ---- */
#define POS_BYTES offsetof(board_t, stack)

static inline void save_pos(const board_t* from, void* to) { memcpy(to, from, POS_BYTES); }
static inline void load_pos(const void* from, board_t* to) { memcpy(to, from, POS_BYTES); to->stack_ptr = 0; }

static bool side_in_check(board_t* board) {
    return board->side == white ?
           is_square_attacked_by_black(board, trailing_zero_count(board->pieces[K])) :
           is_square_attacked_by_white(board, trailing_zero_count(board->pieces[k]));
}

static bool is_legal(board_t* board, move_t mv) {
    uint8_t saved[POS_BYTES];
    save_pos(board, saved);
    const bool legal = make_move(board, mv);
    load_pos(saved, board);
    return legal;
}

static void play(board_t* board, move_t mv) {
    make_move(board, mv);
    board->stack_ptr = 0;
}

static void null_move(board_t* board) {
    /* python-chess's null move: the side changes, the en passant square goes (the hash isn't used here). */
    board->en_passant = none_sq;
    board->side ^= 1;
}

/* A FEN header's position, false if it isn't usable (python-chess would refuse it and skip the game). */
static bool set_fen(board_t* board, const char* fen, size_t len) {
    memset(board, 0, POS_BYTES);
    board->en_passant = none_sq;
    size_t i = 0;
    int rank = 0, file = 0;
    for (; i < len && fen[i] != ' '; i++) {
        const char c = fen[i];
        if (c >= '1' && c <= '8') file += c - '0';
        else if (c == '/') { if (file != 8) return false; rank++; file = 0; }
        else {
            static const char pieces[] = "PNBRQKpnbrqk";
            const char* at = c ? strchr(pieces, c) : NULL;
            if (!at || file >= 8 || rank >= 8) return false;
            set_bit(&board->pieces[at - pieces], rank * 8 + file);
            file++;
        }
    }
    if (rank != 7 || file != 8) return false;
    if (pop_count(board->pieces[K]) != 1 || pop_count(board->pieces[k]) != 1) return false;
    if (++i >= len) return false;
    if (fen[i] == 'w') board->side = white; else if (fen[i] == 'b') board->side = black; else return false;
    i += 2;
    for (; i < len && fen[i] != ' '; i++) {
        switch (fen[i]) {
            case 'K': board->castle_flag |= white_king_side; break;
            case 'Q': board->castle_flag |= white_queen_side; break;
            case 'k': board->castle_flag |= black_king_side; break;
            case 'q': board->castle_flag |= black_queen_side; break;
            case '-': break;
            default: return false;
        }
    }
    i++;
    if (i + 1 < len && fen[i] >= 'a' && fen[i] <= 'h' && fen[i + 1] >= '1' && fen[i + 1] <= '8')
        board->en_passant = (8 - (fen[i + 1] - '0')) * 8 + (fen[i] - 'a');
    for (int pc = 0; pc < 12; pc++) board->occupancy[pc < p ? white : black] |= board->pieces[pc];
    board->occupancy[both] = board->occupancy[white] | board->occupancy[black];
    recalculate_hash(board);
    return true;
}

static board_t start_position;

/* ---- SAN (python-chess Board.parse_san, on a token MOVETEXT_REGEX matched) ---- */

static inline int piece_type(int pc) { return pc % 6; }      /* P N B R Q K = 0..5 */
static int type_of_letter(char c) {
    switch (c) {
        case 'p': case 'P': return 0;
        case 'n': case 'N': return 1;
        case 'b': case 'B': return 2;
        case 'r': case 'R': return 3;
        case 'q': case 'Q': return 4;
        case 'k': case 'K': return 5;
        default: return -1;
    }
}
static inline bool one_of(char c, const char* set) { return c && strchr(set, c); }

typedef struct { char piece, file, rank; int to; char promotion; } san_t;

/* [NBKRQ]?[a-h]?[1-8]?[\-x]?[a-h][1-8](?:=?[nbrqkNBRQK])? matched as the regex does (greedy, backtracking):
 * the length matched, 0 if none. The parts are those SAN_REGEX then finds in the token. */
static size_t match_san(const char* s, const char* end, san_t* san) {
    const size_t n = (size_t) (end - s);
    for (int a = 1; a >= 0; a--) {
        size_t i = 0;
        if (a) { if (i < n && one_of(s[i], "NBKRQ")) i++; else continue; }
        const size_t ia = i;
        for (int f = 1; f >= 0; f--) {
            i = ia;
            if (f) { if (i < n && s[i] >= 'a' && s[i] <= 'h') i++; else continue; }
            const size_t iff = i;
            for (int r = 1; r >= 0; r--) {
                i = iff;
                if (r) { if (i < n && s[i] >= '1' && s[i] <= '8') i++; else continue; }
                const size_t ir = i;
                for (int x = 1; x >= 0; x--) {
                    i = ir;
                    if (x) { if (i < n && (s[i] == '-' || s[i] == 'x')) i++; else continue; }
                    if (i + 1 < n && s[i] >= 'a' && s[i] <= 'h' && s[i + 1] >= '1' && s[i + 1] <= '8') {
                        san->piece = a ? s[0] : 0;
                        san->file = f ? s[ia] : 0;
                        san->rank = r ? s[iff] : 0;
                        san->to = (8 - (s[i + 1] - '0')) * 8 + (s[i] - 'a');
                        i += 2;
                        san->promotion = 0;
                        if (i + 1 < n && s[i] == '=' && one_of(s[i + 1], "nbrqkNBRQK")) {
                            san->promotion = s[i + 1]; i += 2;
                        } else if (i < n && one_of(s[i], "nbrqkNBRQK")) {
                            san->promotion = s[i]; i += 1;
                        }
                        return i;
                    }
                }
            }
        }
    }
    return 0;
}

/* The legal move for a SAN, 0 if illegal or ambiguous (python-chess raises, and the reader skips the rest). */
static move_t parse_san(board_t* board, const san_t* san) {
    move_buffer_t buffer;
    generate_moves(board, &buffer);
    const int own = board->side;
    const int promotion = san->promotion ? type_of_letter(san->promotion) : -1;   /* -1: none */

    if (!san->piece && san->file && san->rank) {
        /* Fully specified (find_move): a pawn to the back rank with no piece becomes a queen, then is refused. */
        const int from = (8 - (san->rank - '0')) * 8 + (san->file - 'a');
        int want = promotion;
        bitboard pawns = board->pieces[P] | board->pieces[p];
        const bool pawn = is_bit_set(&pawns, from);
        if (want < 0 && pawn && (san->to < 8 || san->to >= 56)) want = 4;
        for (int i = 0; i < buffer.index; i++) {
            const move_t mv = buffer.moves[i];
            if ((int) get_source_square(mv) != from || (int) get_target_square(mv) != san->to) continue;
            const int promo = get_promoted_piece(mv) ? piece_type(get_promoted_piece(mv)) : -1;
            if (promo != want) continue;
            if (!is_legal(board, mv)) return 0;
            return promo == promotion ? mv : 0;
        }
        return 0;
    }

    if (is_bit_set(&board->occupancy[own], san->to)) return 0;     /* to_mask excludes our own pieces. */
    const int type = san->piece ? type_of_letter(san->piece) : 0;
    const int need_file = san->file ? san->file - 'a' : (san->piece ? -1 : (san->to & 7));
    const int need_row = san->rank ? 8 - (san->rank - '0') : -1;
    move_t found = 0;
    for (int i = 0; i < buffer.index; i++) {
        const move_t mv = buffer.moves[i];
        if ((int) get_target_square(mv) != san->to) continue;
        if (piece_type(get_piece_moved(mv)) != type) continue;
        const int from = get_source_square(mv);
        if (need_file >= 0 && (from & 7) != need_file) continue;
        if (need_row >= 0 && (from >> 3) != need_row) continue;
        const int promo = get_promoted_piece(mv) ? piece_type(get_promoted_piece(mv)) : -1;
        if (promo != promotion) continue;
        if (!is_legal(board, mv)) continue;
        if (found) return 0;        /* Ambiguous. */
        found = mv;
    }
    return found;
}

static move_t parse_castle(board_t* board, bool king_side) {
    move_buffer_t buffer;
    generate_moves(board, &buffer);
    for (int i = 0; i < buffer.index; i++) {
        const move_t mv = buffer.moves[i];
        if (king_side ? get_king_side_castle_flag(mv) : get_queen_side_castle_flag(mv))
            return is_legal(board, mv) ? mv : 0;
    }
    return 0;
}

/* ---- [%eval] (python-chess EVAL_REGEX and GameNode.eval): the first valid one in a comment ---- */
enum { EVAL_NONE, EVAL_CP, EVAL_MATE };

static inline bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
static inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

/* ',\d+' then ']' at q: the position after the ']', or NULL. */
static const char* eval_tail(const char* q, const char* end) {
    if (q < end && *q == ',') {
        const char* d = ++q;
        while (q < end && is_digit(*q)) q++;
        if (q == d) return NULL;
    }
    return q < end && *q == ']' ? q + 1 : NULL;
}

static int find_eval(const char* s, const char* end, int64_t* cp) {
    for (const char* at = s; at < end && (at = memmem(at, (size_t) (end - at), "[%eval", 6)) != NULL; at++) {
        const char* q = at + 6;
        if (q >= end || !is_space(*q)) continue;
        q++;
        if (q < end && *q == '#') {
            q++;
            if (q < end && (*q == '+' || *q == '-')) q++;
            const char* d = q;
            while (q < end && is_digit(*q)) q++;
            if (q != d && eval_tail(q, end)) return EVAL_MATE;
            continue;
        }
        bool negative = false;
        if (q < end && (*q == '+' || *q == '-')) { negative = *q == '-'; q++; }
        const char* d = q;
        int64_t whole = 0;
        while (q < end && is_digit(*q)) { if (q - d < 12) whole = whole * 10 + (*q - '0'); q++; }
        const int whole_digits = (int) (q - d);
        int frac_digits = 0, frac = 0;
        bool dot = false;
        if (q < end && *q == '.') {
            dot = true; q++;
            while (q < end && is_digit(*q)) { if (frac_digits < 2) frac = frac * 10 + (*q - '0'); frac_digits++; q++; }
        }
        /* \d{0,10}\.\d{1,2} | \d{1,10}\.?  (then the tail: no backtracking can make a longer number fit). */
        const bool valid = whole_digits <= 10 &&
                           ((dot && frac_digits >= 1 && frac_digits <= 2) || (whole_digits >= 1 && frac_digits == 0));
        if (!valid || !eval_tail(q, end)) continue;
        /* round(float(x) * 100), exact for at most two decimals. */
        const int64_t hundredths = whole * 100 + (frac_digits == 1 ? frac * 10 : frac);
        *cp = negative ? -hundredths : hundredths;
        return EVAL_CP;
    }
    return EVAL_NONE;
}

/* ---- Sampling (only when --sample < 1) ---- */
static uint64_t splitmix(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static uint64_t text_hash(const char* s, const char* e) {
    uint64_t h = 1469598103934665603ULL;
    for (; s < e; s++) h = (h ^ (unsigned char) *s) * 1099511628211ULL;
    return h;
}

/* ---- One game: python-chess read_game (with GameBuilder) and lichess.py Converter.lines ---- */

typedef struct {
    board_t board;
    int moves;              /* Length of python-chess's move_stack (whether '(' opens a variation). */
    uint8_t* history;       /* Position after each move, 0..moves (to start a variation: copy(), pop()). */
    int history_capacity;
} line_t;

typedef struct {
    output_t out;
    line_t* lines;          /* python-chess's board_stack. */
    int lines_capacity;
} worker_t;

static void history_set(line_t* line, int index, const board_t* board) {
    if (index >= line->history_capacity) {
        line->history_capacity = index + 64 > 2 * line->history_capacity ? index + 64 : 2 * line->history_capacity;
        line->history = xrealloc(line->history, (size_t) line->history_capacity * POS_BYTES);
    }
    save_pos(board, line->history + (size_t) index * POS_BYTES);
}

static line_t* push_line(worker_t* w, int* depth) {
    if (*depth == w->lines_capacity) {
        const int old = w->lines_capacity;
        w->lines_capacity = old ? old * 2 : 8;
        w->lines = xrealloc(w->lines, (size_t) w->lines_capacity * sizeof(line_t));
        memset(w->lines + old, 0, (size_t) (w->lines_capacity - old) * sizeof(line_t));
    }
    return &w->lines[(*depth)++];
}

static const char* next_line(const char* s, const char* end) {
    const char* nl = memchr(s, '\n', (size_t) (end - s));
    return nl ? nl + 1 : end;
}

/* str.isspace() of a line ('' is False, which happens only at the end). */
static bool line_is_space(const char* s, const char* e) {
    if (s == e) return false;
    for (; s < e; s++) if (!is_space(*s) && !(*s >= 0x1c && *s <= 0x1f)) return false;
    return true;
}

/* TAG_REGEX ^\[([A-Za-z0-9][A-Za-z0-9_+#=:-]*)\s+\"([^\r]*)\"\]\s*$ */
static bool match_tag(const char* s, const char* e, const char** name, size_t* name_len,
                      const char** value, size_t* value_len) {
    #define TAG_CHAR(c) (((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z') || is_digit(c))
    if (s >= e || *s != '[') return false;
    const char* q = s + 1;
    if (q >= e || !TAG_CHAR(*q)) return false;
    const char* n0 = q++;
    while (q < e && (TAG_CHAR(*q) || one_of(*q, "_+#=:-"))) q++;
    *name = n0; *name_len = (size_t) (q - n0);
    const char* ws = q;
    while (q < e && is_space(*q)) q++;
    if (q == ws || q >= e || *q != '"') return false;
    const char* v0 = q + 1;
    const char* t = e;
    while (t > v0 && is_space(t[-1])) t--;
    if (t - v0 < 2 || t[-1] != ']' || t[-2] != '"') return false;
    const char* v1 = t - 2;
    if (memchr(v0, '\r', (size_t) (v1 - v0))) return false;
    *value = v0; *value_len = (size_t) (v1 - v0);
    return true;
    #undef TAG_CHAR
}

/* Python's int() of a header value: optional whitespace and sign, digits (single underscores between). */
static bool parse_int(const char* s, size_t n, long* value) {
    size_t i = 0;
    while (i < n && is_space(s[i])) i++;
    bool negative = false;
    if (i < n && (s[i] == '+' || s[i] == '-')) negative = s[i++] == '-';
    long v = 0;
    int digits = 0;
    bool underscore = false;
    for (; i < n && (is_digit(s[i]) || s[i] == '_'); i++) {
        if (s[i] == '_') { if (!digits || underscore) return false; underscore = true; continue; }
        underscore = false;
        if (v < 100000000000L) v = v * 10 + (s[i] - '0');
        digits++;
    }
    if (!digits || underscore) return false;
    while (i < n && is_space(s[i])) i++;
    if (i != n) return false;
    *value = negative ? -v : v;
    return true;
}

static int result_x2(const char* s, size_t n) {       /* White's result * 2, -1 if none. */
    if (n == 3 && !memcmp(s, "1-0", 3)) return 2;
    if (n == 3 && !memcmp(s, "0-1", 3)) return 0;
    if (n == 7 && !memcmp(s, "1/2-1/2", 7)) return 1;
    return -1;
}

static void emit(output_t* out, board_t* board, int64_t cp, uint64_t game_key, int ply) {
    if (opt_sample < 1.0) {
        const uint64_t h = splitmix(game_key ^ splitmix(opt_seed * 0x100000001b3ULL + (uint64_t) ply));
        if ((double) (h >> 11) * (1.0 / 9007199254740992.0) >= opt_sample) return;
    }
    record_t rec;
    int n = 0;
    bitboard occupied = board->occupancy[both];
    while (occupied) {
        const int sq = get_lsb_and_pop_bit(&occupied);      /* a8 = 0 first: the FEN's order. */
        for (int pc = 0; pc < 12; pc++) {
            if (is_bit_set(&board->pieces[pc], sq)) {
                if (n < MAX_ACTIVE) rec.pieces[n++] = (uint16_t) (pc * 64 + sq);
                break;
            }
        }
    }
    for (int i = n; i < MAX_ACTIVE; i++) rec.pieces[i] = PAD;
    int64_t score = board->side == black ? -cp : cp;
    if (score > 32767) score = 32767;
    if (score < -32767) score = -32767;
    rec.score = (int16_t) score;
    rec.result = 0;         /* Set at the end of the game. */
    rec.side = (uint8_t) board->side;
    push_record(out, &rec);
}

typedef enum { T_NONE, T_SAN, T_CASTLE_SHORT, T_CASTLE_LONG, T_NULL, T_DROP, T_COMMENT, T_SEMICOLON, T_NAG,
               T_OPEN, T_CLOSE, T_RESULT, T_ANNOTATION } token_kind;

/* The MOVETEXT_REGEX token at s (the alternatives in its order), its length in *len. */
static token_kind next_token(const char* s, const char* e, san_t* san, size_t* len) {
    const char c = *s;
    const size_t n = (size_t) (e - s);
    if ((*len = match_san(s, e, san)) != 0) return T_SAN;
    if (n >= 4 && one_of(c, "PNBRQK") && s[1] == '@' && s[2] >= 'a' && s[2] <= 'h' && s[3] >= '1' && s[3] <= '8') { *len = 4; return T_DROP; }
    if (n >= 3 && c == '@' && s[1] >= 'a' && s[1] <= 'h' && s[2] >= '1' && s[2] <= '8') { *len = 3; return T_DROP; }
    if (n >= 2 && c == '-' && s[1] == '-') { *len = 2; return T_NULL; }
    if (n >= 2 && c == 'Z' && s[1] == '0') { *len = 2; return T_NULL; }
    if (n >= 4 && !memcmp(s, "0000", 4)) { *len = 4; return T_NULL; }
    if (n >= 4 && !memcmp(s, "@@@@", 4)) { *len = 4; return T_NULL; }
    if (n >= 3 && (!memcmp(s, "O-O", 3) || !memcmp(s, "0-0", 3))) {
        if (n >= 5 && s[3] == '-' && s[4] == c) { *len = 5; return T_CASTLE_LONG; }
        *len = 3; return T_CASTLE_SHORT;
    }
    if (c == '{') { *len = 1; return T_COMMENT; }
    if (c == ';') { *len = n; return T_SEMICOLON; }
    if (c == '$' && n >= 2 && is_digit(s[1])) { size_t i = 1; while (i < n && is_digit(s[i])) i++; *len = i; return T_NAG; }
    if (c == '(') { *len = 1; return T_OPEN; }
    if (c == ')') { *len = 1; return T_CLOSE; }
    if (c == '*') { *len = 1; return T_RESULT; }
    if (n >= 3 && (!memcmp(s, "1-0", 3) || !memcmp(s, "0-1", 3))) { *len = 3; return T_RESULT; }
    if (n >= 7 && !memcmp(s, "1/2-1/2", 7)) { *len = 7; return T_RESULT; }
    if (c == '?' || c == '!') { *len = n >= 2 && (s[1] == '?' || s[1] == '!') ? 2 : 1; return T_ANNOTATION; }
    *len = 1;
    return T_NONE;
}

/* Converts one game's text (from its '[Event ' line to the next game's). Returns whether lichess.py counts it
 * (it has '[%eval '). */
static bool convert_game(worker_t* w, const char* text, const char* end) {
    if (!memmem(text, (size_t) (end - text), "[%eval ", 7)) return false;
    output_t* out = &w->out;

    /* Headers. */
    const char* result = "*"; size_t result_len = 1;       /* python-chess's default Result header. */
    const char* white_elo = NULL; size_t white_elo_len = 0;
    const char* black_elo = NULL; size_t black_elo_len = 0;
    const char* fen = NULL; size_t fen_len = 0;

    const char* line = text;
    const char* line_end = next_line(line, end);
    #define NEXT_LINE() do { line = line_end; line_end = next_line(line, end); } while (0)
    while (line < end && (line_is_space(line, line_end) || *line == '%' || *line == ';')) NEXT_LINE();
    int consecutive_empty = 0;
    while (line < end) {
        if (*line == '%' || *line == ';') { NEXT_LINE(); continue; }
        if (consecutive_empty < 1 && line_is_space(line, line_end)) { consecutive_empty++; NEXT_LINE(); continue; }
        if (*line != '[') break;
        consecutive_empty = 0;
        const char *name, *value; size_t name_len, value_len;
        if (match_tag(line, line_end, &name, &name_len, &value, &value_len)) {
            if (name_len == 6 && !memcmp(name, "Result", 6)) { result = value; result_len = value_len; }
            else if (name_len == 8 && !memcmp(name, "WhiteElo", 8)) { white_elo = value; white_elo_len = value_len; }
            else if (name_len == 8 && !memcmp(name, "BlackElo", 8)) { black_elo = value; black_elo_len = value_len; }
            else if (name_len == 3 && !memcmp(name, "FEN", 3)) { fen = value; fen_len = value_len; }
        }
        NEXT_LINE();
    }

    /* The rating filter (the result filter waits: the movetext can set a '*' result). */
    if (opt_min_elo) {
        long we, be;
        if (!white_elo || !black_elo || !parse_int(white_elo, white_elo_len, &we) ||
            !parse_int(black_elo, black_elo_len, &be) || (we < be ? we : be) < opt_min_elo) {
            end_game(out);
            return true;
        }
    }

    const size_t first_record = out->count;
    int depth = 0;
    line_t* main_line = push_line(w, &depth);
    if (fen) {
        if (!set_fen(&main_line->board, fen, fen_len)) { end_game(out); return true; }
    } else {
        load_pos(&start_position, &main_line->board);
    }
    main_line->board.stack_ptr = 0;
    main_line->moves = 0;
    history_set(main_line, 0, &main_line->board);

    int node_depth = 0;             /* len(GameBuilder.variation_stack) - 1. */
    bool in_variation = false;      /* GameBuilder.in_variation. */
    int skip = 0;                   /* skip_variation_depth. */
    int ply = 0;                    /* The mainline's current node, and its eval. */
    int eval_kind = EVAL_NONE;
    int64_t eval_cp = 0;
    const uint64_t game_key = opt_sample < 1.0 ? text_hash(text, end) : 0;

    /* Movetext. */
    bool fresh_line = true;
    bool broken = false;            /* python-chess would raise (an unbalanced variation stack): stop. */
    while (line < end && !broken) {
        if (fresh_line) {
            if (*line == '%' || *line == ';') { NEXT_LINE(); continue; }
            if (line_is_space(line, line_end)) break;       /* An empty line ends the game. */
        }
        fresh_line = true;
        bool comment_ended = false;
        for (const char* s = line; s < line_end && !broken; ) {
            san_t san;
            size_t len;
            const token_kind kind = next_token(s, line_end, &san, &len);
            if (kind == T_COMMENT) {
                /* To the first '}', across lines; the rest of that line goes on without the empty-line check. */
                const char* body = s + 1;
                const char* close = memchr(body, '}', (size_t) (end - body));
                const char* body_end = close ? close : end;
                if (!skip && node_depth == 0 && in_variation && eval_kind == EVAL_NONE)
                    eval_kind = find_eval(body, body_end, &eval_cp);
                if (close) { line = close + 1; line_end = next_line(line, end); }
                else { line = line_end = end; }
                fresh_line = false;
                comment_ended = true;
                break;
            }
            s += len;
            if (kind == T_OPEN) {
                if (skip) skip++;
                else if (w->lines[depth - 1].moves) {
                    node_depth++;
                    in_variation = false;
                    line_t* var = push_line(w, &depth);
                    const line_t* parent = &w->lines[depth - 2];
                    var->moves = parent->moves - 1;
                    for (int i = 0; i <= var->moves; i++) {
                        load_pos(parent->history + (size_t) i * POS_BYTES, &var->board);
                        history_set(var, i, &var->board);
                    }
                }
                continue;
            }
            if (kind == T_CLOSE) {
                if (skip == 1) { skip = 0; if (--node_depth < 0) broken = true; }
                else if (skip) skip--;
                else if (depth > 1) { if (--node_depth < 0) broken = true; depth--; }
                continue;
            }
            if (skip) continue;
            if (kind == T_SEMICOLON) break;
            if (kind == T_NAG || kind == T_ANNOTATION || kind == T_NONE) continue;
            if (kind == T_RESULT && depth == 1) {
                if (result_len == 1 && result[0] == '*') { result = s - len; result_len = len; }
                continue;
            }

            /* A move (a result inside a variation is parsed as one, and is invalid). */
            line_t* cur = &w->lines[depth - 1];
            move_t mv = 0;
            if (kind == T_SAN) mv = parse_san(&cur->board, &san);
            else if (kind == T_CASTLE_SHORT || kind == T_CASTLE_LONG) mv = parse_castle(&cur->board, kind == T_CASTLE_SHORT);
            if (!mv && kind != T_NULL) { skip = 1; continue; }

            if (node_depth == 0) {
                /* The mainline node 'ply' has its next move: keep its position if it qualifies. */
                if (ply > opt_skip_plies && eval_kind == EVAL_CP && (eval_cp < 0 ? -eval_cp : eval_cp) <= opt_max_score &&
                    !side_in_check(&cur->board) && !(mv && (get_capture_flag(mv) || get_promoted_piece(mv))))
                    emit(out, &cur->board, eval_cp, game_key, ply);
                ply++;
                eval_kind = EVAL_NONE;
            }
            if (mv) play(&cur->board, mv); else null_move(&cur->board);
            cur->moves++;
            history_set(cur, cur->moves, &cur->board);
            in_variation = true;
        }
        if (!comment_ended) NEXT_LINE();
    }
    #undef NEXT_LINE

    const int white_x2 = result_x2(result, result_len);
    if (white_x2 < 0) out->count = first_record;
    for (size_t i = first_record; i < out->count; i++) {
        record_t* rec = &out->records[i];
        rec->result = (uint8_t) (rec->side == black ? 2 - white_x2 : white_x2);
    }
    end_game(out);
    return true;
}

/* All the games of a chunk (whole games: it starts at a game, or at the dump's start, and ends before one). */
static void convert_chunk(worker_t* w, const char* text, size_t len) {
    w->out.count = 0;
    w->out.games = 0;
    const char* end = text + len;
    const char* game = text;
    while (game < end) {
        const char* next = memmem(game, (size_t) (end - game), "\n[Event ", 8);
        const char* game_end = next ? next + 1 : end;
        convert_game(w, game, game_end);
        game = game_end;
    }
}

/* ---- Input: the dump's text, decompressed ---- */
typedef struct {
    FILE* file;
    bool zstd;
    ZSTD_DStream* stream;
    ZSTD_inBuffer in;
    char* in_buffer;
    size_t last_result;
    bool file_eof;
    uint64_t compressed_read, compressed_total;
} input_t;

#define IN_BUFFER (1 << 20)

static bool input_open(input_t* in, const char* path) {
    memset(in, 0, sizeof(*in));
    in->file = fopen(path, "rb");
    if (!in->file) return false;
    struct stat st;
    if (fstat(fileno(in->file), &st) == 0) in->compressed_total = (uint64_t) st.st_size;
    in->in_buffer = xrealloc(NULL, IN_BUFFER);
    unsigned char magic[4] = { 0 };
    const size_t got = fread(magic, 1, 4, in->file);
    /* A zstd frame, or a skippable frame (0x184D2A50..5F, which pzstd-compressed dumps such as Lichess's start
     * with), or a .zst name. */
    const size_t path_len = strlen(path);
    in->zstd = (got == 4 && magic[0] == 0x28 && magic[1] == 0xB5 && magic[2] == 0x2F && magic[3] == 0xFD) ||
               (got == 4 && (magic[0] & 0xF0) == 0x50 && magic[1] == 0x2A && magic[2] == 0x4D && magic[3] == 0x18) ||
               (path_len > 4 && !strcmp(path + path_len - 4, ".zst"));
    memcpy(in->in_buffer, magic, got);
    in->in.src = in->in_buffer;
    in->in.size = got;
    in->in.pos = 0;
    in->compressed_read = got;
    if (in->zstd) {
        in->stream = ZSTD_createDStream();
        ZSTD_initDStream(in->stream);
    }
    return true;
}

/* Up to 'cap' bytes of text into dst; 0 at the end. */
static size_t input_read(input_t* in, char* dst, size_t cap) {
    if (!in->zstd) {
        size_t n = 0;
        if (in->in.pos < in->in.size) {
            n = in->in.size - in->in.pos < cap ? in->in.size - in->in.pos : cap;
            memcpy(dst, in->in_buffer + in->in.pos, n);
            in->in.pos += n;
        }
        const size_t more = fread(dst + n, 1, cap - n, in->file);
        in->compressed_read += more;
        return n + more;
    }
    ZSTD_outBuffer out = { dst, cap, 0 };
    while (out.pos == 0) {
        if (in->in.pos == in->in.size && !in->file_eof) {
            in->in.size = fread(in->in_buffer, 1, IN_BUFFER, in->file);
            in->in.pos = 0;
            in->compressed_read += in->in.size;
            if (in->in.size == 0) in->file_eof = true;
        }
        const size_t in_before = in->in.pos;
        const size_t r = ZSTD_decompressStream(in->stream, &out, &in->in);
        if (ZSTD_isError(r)) {
            fprintf(stderr, "lichess_convert: zstd: %s\n", ZSTD_getErrorName(r));
            exit(1);
        }
        /* 0 when a frame is complete; a call with nothing to do after that returns the next frame's hint, so
         * only calls that made progress count. */
        if (out.pos > 0 || in->in.pos > in_before) in->last_result = r;
        if (out.pos == 0 && in->file_eof && in->in.pos == in->in.size) {
            if (in->last_result != 0) { fprintf(stderr, "lichess_convert: the dump is truncated\n"); exit(1); }
            return 0;
        }
    }
    return out.pos;
}

/* ---- Pipeline ---- */
#define CHUNK_TARGET (16u << 20)
#define MAX_CHUNK ((size_t) 256 << 20)

typedef enum { SLOT_FREE, SLOT_READY, SLOT_DONE } slot_state;

typedef struct {
    char* text;
    size_t len, capacity;
    uint64_t seq;
    slot_state state;
    output_t* out;          /* The worker's output, handed over with the slot. */
    output_t result;        /* Swapped with the worker's output when done. */
} slot_t;

static slot_t* slots;
static int slot_count;
static uint64_t jobs_ready, next_job;
static bool input_done, stop_workers;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t job_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t done_cond = PTHREAD_COND_INITIALIZER;

static void* worker_main(void* arg) {
    worker_t* w = arg;
    for (;;) {
        pthread_mutex_lock(&lock);
        while (next_job == jobs_ready && !input_done && !stop_workers) pthread_cond_wait(&job_cond, &lock);
        if (stop_workers || next_job == jobs_ready) { pthread_mutex_unlock(&lock); return NULL; }
        slot_t* slot = &slots[next_job++ % (uint64_t) slot_count];
        pthread_mutex_unlock(&lock);

        convert_chunk(w, slot->text, slot->len);
        /* Hand the records over by swapping buffers. */
        const output_t tmp = slot->result;
        slot->result = w->out;
        w->out = tmp;

        pthread_mutex_lock(&lock);
        slot->state = SLOT_DONE;
        pthread_cond_signal(&done_cond);
        pthread_mutex_unlock(&lock);
    }
}

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void usage(void) {
    fprintf(stderr, "usage: lichess_convert [--min-elo N] [--skip-plies N] [--max-score N] [--sample F] [--seed N]\n"
                    "                       [--max-positions N] [--threads N] <dump.pgn.zst|dump.pgn> <out.bin>\n");
    exit(2);
}

int main(int argc, char** argv) {
    const char* in_path = NULL;
    const char* out_path = NULL;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (a[0] == '-' && a[1] == '-') {
            if (i + 1 >= argc) usage();
            const char* v = argv[++i];
            if (!strcmp(a, "--min-elo")) opt_min_elo = atoi(v);
            else if (!strcmp(a, "--skip-plies")) opt_skip_plies = atoi(v);
            else if (!strcmp(a, "--max-score")) opt_max_score = atoi(v);
            else if (!strcmp(a, "--sample")) opt_sample = atof(v);
            else if (!strcmp(a, "--seed")) opt_seed = strtoull(v, NULL, 10);
            else if (!strcmp(a, "--max-positions")) opt_max_positions = strtoull(v, NULL, 10);
            else if (!strcmp(a, "--threads")) opt_threads = atoi(v);
            else usage();
        } else if (!in_path) in_path = a;
        else if (!out_path) out_path = a;
        else usage();
    }
    if (!in_path || !out_path || opt_skip_plies < 0 || opt_min_elo < 0 || opt_max_score < 0 ||
        opt_sample < 0.0 || opt_sample > 1.0)
        usage();
    if (opt_threads <= 0) {
        const long cpus = sysconf(_SC_NPROCESSORS_ONLN);
        opt_threads = cpus > 1 ? (int) cpus - 1 : 1;
    }

    init_attack_table();
    init_zobrist_key();
    if (!set_fen(&start_position, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 56)) return 1;

    input_t in;
    if (!input_open(&in, in_path)) { fprintf(stderr, "lichess_convert: %s: %s\n", in_path, strerror(errno)); return 1; }
    char part_path[4096];
    snprintf(part_path, sizeof(part_path), "%s.part", out_path);
    FILE* out = fopen(part_path, "wb");
    if (!out) { fprintf(stderr, "lichess_convert: %s: %s\n", part_path, strerror(errno)); return 1; }
    setvbuf(out, NULL, _IOFBF, 16 << 20);
    fwrite(MAGIC, 1, sizeof(MAGIC), out);

    slot_count = opt_threads + 2;
    slots = calloc((size_t) slot_count, sizeof(slot_t));
    worker_t* workers = calloc((size_t) opt_threads, sizeof(worker_t));
    pthread_t* threads = calloc((size_t) opt_threads, sizeof(pthread_t));
    for (int i = 0; i < opt_threads; i++) pthread_create(&threads[i], NULL, worker_main, &workers[i]);

    char* carry = NULL;
    size_t carry_len = 0, carry_capacity = 0;
    uint64_t next_write = 0, games = 0, positions = 0;
    bool eof = false, limit_reached = false;
    const double start = now_seconds();
    double last_report = start;

    /* Writes the finished chunks in order. Returns with the lock held. */
    #define FLUSH_DONE() do { \
        while (next_write < jobs_ready && slots[next_write % (uint64_t) slot_count].state == SLOT_DONE && !limit_reached) { \
            slot_t* ws = &slots[next_write % (uint64_t) slot_count]; \
            pthread_mutex_unlock(&lock); \
            size_t keep = ws->result.count, keep_games = ws->result.games; \
            if (opt_max_positions) { \
                for (size_t g = 0; g < ws->result.games; g++) { \
                    if (positions + ws->result.game_ends[g] >= opt_max_positions) { \
                        keep = ws->result.game_ends[g]; keep_games = g + 1; limit_reached = true; break; \
                    } \
                } \
            } \
            fwrite(ws->result.records, sizeof(record_t), keep, out); \
            positions += keep; games += keep_games; \
            pthread_mutex_lock(&lock); \
            ws->state = SLOT_FREE; \
            next_write++; \
        } \
    } while (0)

    for (uint64_t seq = 0; !eof && !limit_reached; seq++) {
        slot_t* slot = &slots[seq % (uint64_t) slot_count];
        pthread_mutex_lock(&lock);
        FLUSH_DONE();
        while (slot->state != SLOT_FREE && !limit_reached) {
            pthread_cond_wait(&done_cond, &lock);
            FLUSH_DONE();
        }
        pthread_mutex_unlock(&lock);
        if (limit_reached) break;

        /* Fill: the carried-over game start, then text until the target, cut before the last game start. */
        if (slot->capacity < CHUNK_TARGET + carry_len + (1 << 20)) {
            slot->capacity = CHUNK_TARGET + carry_len + (8 << 20);
            slot->text = xrealloc(slot->text, slot->capacity);
        }
        memcpy(slot->text, carry, carry_len);
        slot->len = carry_len;
        carry_len = 0;
        for (;;) {
            while (slot->len < CHUNK_TARGET) {
                const size_t got = input_read(&in, slot->text + slot->len, slot->capacity - slot->len);
                if (got == 0) { eof = true; break; }
                slot->len += got;
            }
            if (eof) break;
            /* The last game start ("\n[Event ") after the first byte. */
            size_t cut = 0;
            for (size_t i = slot->len >= 8 ? slot->len - 8 : 0; i > 0; i--) {
                if (slot->text[i] == '\n' && !memcmp(slot->text + i + 1, "[Event ", 7)) { cut = i + 1; break; }
            }
            if (cut) {
                carry_len = slot->len - cut;
                if (carry_len > carry_capacity) { carry_capacity = carry_len * 2; carry = xrealloc(carry, carry_capacity); }
                memcpy(carry, slot->text + cut, carry_len);
                slot->len = cut;
                break;
            }
            /* One game bigger than the chunk: grow it, within reason (a PGN game is kilobytes; this is not one). */
            if (slot->capacity >= MAX_CHUNK) {
                fprintf(stderr, "lichess_convert: no game start ('[Event ') in %zu MB of text: not a PGN dump?\n",
                        slot->len >> 20);
                exit(1);
            }
            slot->capacity *= 2;
            slot->text = xrealloc(slot->text, slot->capacity);
            if (slot->len >= slot->capacity / 2) {
                const size_t got = input_read(&in, slot->text + slot->len, slot->capacity - slot->len);
                if (got == 0) { eof = true; break; }
                slot->len += got;
            }
        }

        pthread_mutex_lock(&lock);
        slot->seq = seq;
        slot->state = SLOT_READY;
        jobs_ready = seq + 1;
        pthread_cond_signal(&job_cond);
        pthread_mutex_unlock(&lock);

        const double t = now_seconds();
        if (t - last_report >= 10.0) {
            last_report = t;
            const double fraction = in.compressed_total ? (double) in.compressed_read / (double) in.compressed_total : 0.0;
            const double elapsed = t - start;
            fprintf(stderr, "%5.1f%%  %6.0f s  %8.1f MB/s compressed  %12llu games  %14llu positions  ETA %.0f s\n",
                    100.0 * fraction, elapsed, (double) in.compressed_read / elapsed / 1e6,
                    (unsigned long long) games, (unsigned long long) positions,
                    fraction > 0 ? elapsed / fraction - elapsed : 0.0);
        }
    }

    pthread_mutex_lock(&lock);
    input_done = true;
    pthread_cond_broadcast(&job_cond);
    while (next_write < jobs_ready && !limit_reached) {
        FLUSH_DONE();
        if (next_write < jobs_ready && !limit_reached) pthread_cond_wait(&done_cond, &lock);
    }
    if (limit_reached) stop_workers = true;
    pthread_cond_broadcast(&job_cond);
    pthread_mutex_unlock(&lock);
    #undef FLUSH_DONE
    for (int i = 0; i < opt_threads; i++) pthread_join(threads[i], NULL);

    if (fclose(out) != 0) { fprintf(stderr, "lichess_convert: writing %s failed\n", part_path); return 1; }
    if (rename(part_path, out_path) != 0) { fprintf(stderr, "lichess_convert: rename: %s\n", strerror(errno)); return 1; }
    const double elapsed = now_seconds() - start;
    fprintf(stderr, "done: %llu games, %llu positions, %.0f s\n", (unsigned long long) games,
            (unsigned long long) positions, elapsed);
    printf("%llu games, %llu positions\n", (unsigned long long) games, (unsigned long long) positions);
    return 0;
}
