#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "datagen.h"
#include "board.h"
#include "move_generator.h"
#include "search.h"
#include "search_state.h"
#include "transposition.h"
#include "random.h"
#include "uci.h"

#define DATAGEN_MAX_PLIES 1000


typedef struct {
    char fen[100];      /* Text format only. */
    int score;          /* White's point of view. */
    uint8_t record[DATAGEN_RECORD_SIZE];   /* Binary format only, the result is filled in at the end. */
} datagen_position_t;

#define RECORD_PAD 0xFFFF
#define RECORD_SCORE 64     /* Offsets in a record. */
#define RECORD_RESULT 66
#define RECORD_SIDE 67

static void put_u16(uint8_t* at, uint16_t value) {
    at[0] = (uint8_t) (value & 0xFF);
    at[1] = (uint8_t) (value >> 8);
}

/* The binary record of 'board' with the search score for the side to move (datagen.h). */
static void encode_record(const board_t* board, int score, uint8_t* record) {
    int count = 0;
    for (int sq = 0; sq < 64; sq++) {
        for (int pc = P; pc <= k; pc++) {
            if (board->pieces[pc] & (1ULL << sq)) {
                put_u16(record + 2 * count++, (uint16_t) (pc * 64 + sq));
                break;
            }
        }
    }
    for (; count < 32; count++) put_u16(record + 2 * count, RECORD_PAD);
    if (score > 32767) score = 32767;
    if (score < -32767) score = -32767;
    put_u16(record + RECORD_SCORE, (uint16_t) (int16_t) score);
    record[RECORD_RESULT] = 1;
    record[RECORD_SIDE] = (uint8_t) board->side;
}


static bool in_check(board_t* board) {
    return board->side == white ?
           is_square_attacked_by_black(board, trailing_zero_count(board->pieces[K])) :
           is_square_attacked_by_white(board, trailing_zero_count(board->pieces[k]));
}

static int legal_moves(board_t* board, move_t* moves) {
    move_buffer_t buffer;
    generate_moves(board, &buffer);
    int n = 0;
    for (int i = 0; i < buffer.index; i++) {
        if (make_move(board, buffer.moves[i])) moves[n++] = buffer.moves[i];
        pop_move(board);
    }
    return n;
}

/* Appends the move in UCI notation (e7e8q) to the 'position ... moves' command. */
static void append_move(char* command, size_t size, move_t mv) {
    size_t n = strlen(command);
    piece promoted = get_promoted_piece(mv);
    snprintf(command + n, size - n, " %s%s", square_to_str[get_source_square(mv)], square_to_str[get_target_square(mv)]);
    if (promoted) {
        n = strlen(command);
        snprintf(command + n, size - n, "%c", tolower(piece_to_char[promoted]));
    }
}

/* Plays one game, returns positions written (0 if the random opening ended the game). */
static uint64_t play_game(FILE* out, const datagen_options_t* options, uint32_t* rng,
                          board_t* board, search_state_t* search_state, datagen_position_t* positions) {
    static char command[DATAGEN_MAX_PLIES * 6 + 64];
    move_t moves[MOVE_BUFFER_SIZE];
    limits_t limits;

    tt_clear();
    init_search_state(search_state);
    snprintf(command, sizeof(command), "position startpos moves");
    parse_position(command, board, search_state);

    /* Random opening, so the games differ. */
    for (int i = 0; i < options->random_plies; i++) {
        int n = legal_moves(board, moves);
        if (n == 0) return 0;
        append_move(command, sizeof(command), moves[next_random_32(rng) % n]);
        parse_position(command, board, search_state);
    }

    int recorded = 0;
    int winning_plies = 0;  /* Consecutive plies with |score| >= DATAGEN_WIN_SCORE, same side. */
    int last_sign = 0;
    double result = 0.5;    /* For white. */
    const int max_plies = options->max_plies < DATAGEN_MAX_PLIES ? options->max_plies : DATAGEN_MAX_PLIES;

    for (int ply = options->random_plies; ; ply++) {
        const bool check = in_check(board);
        if (legal_moves(board, moves) == 0) {
            /* Mated: the side to move lost. Otherwise stalemate. */
            if (check) result = board->side == white ? 0.0 : 1.0;
            break;
        }
        if (is_draw(board, search_state) || ply >= max_plies) break;

        clear_pv_table(search_state);
        tt_new_search();
        reset_time_control(&limits);
        limits.depth = options->depth;
        limits.quiet = true;
        move_t mv = find_best_move(board, search_state, &limits);
        const int score = board->side == white ? search_state->score : -search_state->score;

        if (score >= MATE_SCORE || score <= -MATE_SCORE) {
            result = score > 0 ? 1.0 : 0.0;
            break;
        }

        const int sign = score >= DATAGEN_WIN_SCORE ? 1 : score <= -DATAGEN_WIN_SCORE ? -1 : 0;
        winning_plies = sign != 0 && sign == last_sign ? winning_plies + 1 : (sign != 0);
        last_sign = sign;
        if (winning_plies >= DATAGEN_WIN_PLIES) {
            result = sign > 0 ? 1.0 : 0.0;
            break;
        }

        if (!check && !get_capture_flag(mv) && !get_promoted_piece(mv)) {
            if (options->format == datagen_binary) {
                encode_record(board, search_state->score, positions[recorded].record);
            } else {
                board_to_fen(board, 1 + ply / 2, positions[recorded].fen, sizeof(positions[recorded].fen));
            }
            positions[recorded].score = score;
            recorded++;
        }

        append_move(command, sizeof(command), mv);
        parse_position(command, board, search_state);    }

    for (int i = 0; i < recorded; i++) {
        if (options->format == datagen_binary) {
            /* The game result for the side to move: 2 / 1 / 0. */
            const int white_result = (int) (result * 2.0 + 0.5);
            uint8_t* record = positions[i].record;
            record[RECORD_RESULT] = (uint8_t) (record[RECORD_SIDE] == white ? white_result : 2 - white_result);
            fwrite(record, 1, DATAGEN_RECORD_SIZE, out);
        } else {
            fprintf(out, "%s | %d | %.1f\n", positions[i].fen, positions[i].score, result);
        }
    }
    return (uint64_t) recorded;
}

uint64_t datagen(FILE* out, const datagen_options_t* options) {
    static board_t board;
    static search_state_t search_state;
    static datagen_position_t positions[DATAGEN_MAX_PLIES];
    uint32_t rng = options->seed ? options->seed : random_seed;

    /* A small TT, cleared every game. The previous size is restored after. */
    const size_t previous_mb = (tt_size * sizeof(transposition_node_t)) >> 20;
    init_transposition_table(DATAGEN_HASH_MB);

    uint64_t written = 0;
    for (int game = 0; game < options->games; game++) {
        written += play_game(out, options, &rng, &board, &search_state, positions);
    }
    fflush(out);

    init_transposition_table(previous_mb ? previous_mb : DATAGEN_HASH_MB);
    return written;
}
