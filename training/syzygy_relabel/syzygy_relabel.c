/*
 * syzygy_relabel: replaces the game result of tiki's training positions with the Syzygy tablebase
 * result, where the tables cover the position.
 *
 *     syzygy_relabel --tb DIR [options] IN OUT
 *     syzygy_relabel --tb DIR --stats-only IN
 *
 * IN is a TIKIBIN1 file or a directory of them (every *.bin, .part files left out); OUT is the file or
 * directory to write. The output has the same records in the same order: only the result byte of
 * the positions the tables cover changes, the score (the search's) is kept. Each file is written
 * via <out>.part and renamed when complete; outputs that already exist are skipped (--force
 * rewrites them), so an interrupted run can be started again.
 *
 * A record (see training/tiki_nnue/binary.py) is 68 bytes, little-endian:
 *
 *     pieces  uint16[32]  piece * 64 + square, tiki's piece order (P N B R Q K p n b r q k) and
 *                         square index (a8 = 0 ... h1 = 63), 0xFFFF after the last
 *     score   int16       centipawns for the side to move
 *     result  uint8       2 / 1 / 0 for a win / draw / loss for the side to move
 *     side    uint8       side to move, 0 white, 1 black
 *
 * What a record doesn't hold, and how it's handled:
 *
 *   - Castling rights: the tables don't cover positions that can still castle. A position with a
 *     king and a rook of the same colour on their starting squares might, so it's left unchanged.
 *   - The en passant square: a position where the side to move has a pawn beside an enemy pawn that
 *     could just have advanced two squares is probed both ways (with and without the capture). If
 *     the results differ the position is left unchanged.
 *   - The fifty-move counter: positions are probed as if it were zero. A win that the fifty-move
 *     rule turns into a draw (a "cursed win", or "blessed loss" for the loser) is labelled a draw,
 *     as it would end in a game.
 *
 * The tables are probed with Fathom (https://github.com/jdart1/Fathom, MIT licence), which the
 * Makefile downloads at a fixed commit.
 */

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "tbprobe.h"

#define RECORD 68
#define SLOTS 32
#define PAD 0xFFFF
#define RESULT_OFFSET 66
#define SIDE_OFFSET 67
#define BLOCK_RECORDS (1u << 20)   /* records read, relabelled and written at a time: 68 MB */
#define MAX_THREADS 64

static const char MAGIC[8] = {'T', 'I', 'K', 'I', 'B', 'I', 'N', '1'};

/* Fathom's square index (a1 = 0 ... h8 = 63) of tiki's (a8 = 0 ... h1 = 63). */
#define FATHOM_SQUARE(sq) ((sq) ^ 56)
#define BIT(sq) (1ull << (sq))

enum { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };

typedef struct {
    uint64_t records;
    uint64_t covered;          /* positions with at most --max-pieces pieces */
    uint64_t castling;         /* covered, but left unchanged as they might castle */
    uint64_t en_passant;       /* covered, but the result depends on an en passant capture */
    uint64_t failed;           /* the probe failed (no table for the material, or not a position) */
    uint64_t probed;           /* relabelled: covered less the above */
    uint64_t changed;          /* probed and given a different result */
    uint64_t matrix[3][3];     /* [game result][tablebase result], probed positions */
    uint64_t by_pieces[SLOTS + 1][2];  /* [piece count] probed, changed */
} stats_t;

static unsigned opt_max_pieces;
static unsigned opt_threads;
static bool opt_stats_only;
static bool opt_force;

static void stats_add(stats_t* to, const stats_t* from) {
    to->records += from->records;
    to->covered += from->covered;
    to->castling += from->castling;
    to->en_passant += from->en_passant;
    to->failed += from->failed;
    to->probed += from->probed;
    to->changed += from->changed;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) to->matrix[i][j] += from->matrix[i][j];
    for (int n = 0; n <= SLOTS; n++) {
        to->by_pieces[n][0] += from->by_pieces[n][0];
        to->by_pieces[n][1] += from->by_pieces[n][1];
    }
}

/* Fathom's WDL (from the side to move's point of view) as a result byte: cursed wins and blessed
 * losses are draws under the fifty-move rule. */
static int result_of(unsigned wdl) {
    switch (wdl) {
    case TB_WIN: return 2;
    case TB_LOSS: return 0;
    default: return 1;
    }
}

typedef struct {
    uint64_t colour[2];
    uint64_t type[6];
    bool white_to_move;
} position_t;

static unsigned probe(const position_t* p, unsigned ep) {
    return tb_probe_wdl(p->colour[0], p->colour[1], p->type[KING], p->type[QUEEN], p->type[ROOK],
                        p->type[BISHOP], p->type[KNIGHT], p->type[PAWN], 0, 0, ep, p->white_to_move);
}

/* The en passant squares that might be available to the side to move: an enemy pawn on its fourth
 * rank that could have just come from its second (both squares behind it empty), with a pawn of the
 * side to move beside it. Returns how many were written to 'squares'. */
static int en_passant_candidates(const position_t* p, unsigned squares[8]) {
    const int us = p->white_to_move ? 0 : 1;
    const uint64_t occupied = p->colour[0] | p->colour[1];
    const uint64_t our_pawns = p->type[PAWN] & p->colour[us];
    uint64_t their_pawns = p->type[PAWN] & p->colour[!us];
    const int rank = us == 0 ? 4 : 3;               /* where a pawn that just advanced two stands */
    const int behind = us == 0 ? 8 : -8;            /* towards the square it came from */
    int count = 0;
    while (their_pawns) {
        const int sq = __builtin_ctzll(their_pawns);
        their_pawns &= their_pawns - 1;
        if (sq / 8 != rank) continue;
        const int passed = sq + behind, start = sq + 2 * behind;
        if (occupied & (BIT(passed) | BIT(start))) continue;
        const int file = sq % 8;
        const bool beside = (file > 0 && (our_pawns & BIT(sq - 1))) || (file < 7 && (our_pawns & BIT(sq + 1)));
        if (beside) squares[count++] = (unsigned) passed;
    }
    return count;
}

static void relabel_record(uint8_t* record, stats_t* stats) {
    stats->records++;
    position_t p = {{0}, {0}, record[SIDE_OFFSET] == 0};
    unsigned pieces = 0;
    for (int i = 0; i < SLOTS; i++) {
        const unsigned value = record[2 * i] | (unsigned) record[2 * i + 1] << 8;
        if (value == PAD) break;
        const unsigned piece = value >> 6, sq = FATHOM_SQUARE(value & 63);
        if (piece > 11) {   /* not a piece: leave it */
            pieces = SLOTS + 1;
            break;
        }
        p.colour[piece >= 6] |= BIT(sq);
        p.type[piece % 6] |= BIT(sq);
        pieces++;
    }
    if (pieces > opt_max_pieces) return;
    stats->covered++;

    const uint64_t white_king = p.type[KING] & p.colour[0], black_king = p.type[KING] & p.colour[1];
    const uint64_t white_rooks = p.type[ROOK] & p.colour[0], black_rooks = p.type[ROOK] & p.colour[1];
    if (((white_king & BIT(4)) && (white_rooks & (BIT(0) | BIT(7)))) ||
        ((black_king & BIT(60)) && (black_rooks & (BIT(56) | BIT(63))))) {
        stats->castling++;
        return;
    }
    const unsigned old_result = record[RESULT_OFFSET];
    if (__builtin_popcountll(white_king) != 1 || __builtin_popcountll(black_king) != 1 || old_result > 2) {
        stats->failed++;
        return;
    }

    const unsigned wdl = probe(&p, 0);
    if (wdl == TB_RESULT_FAILED) {
        stats->failed++;
        return;
    }
    const int result = result_of(wdl);
    unsigned squares[8];
    const int candidates = en_passant_candidates(&p, squares);
    for (int i = 0; i < candidates; i++) {
        const unsigned other = probe(&p, squares[i]);
        if (other == TB_RESULT_FAILED) {
            stats->failed++;
            return;
        }
        if (result_of(other) != result) {
            stats->en_passant++;
            return;
        }
    }

    stats->probed++;
    stats->matrix[old_result][result]++;
    stats->by_pieces[pieces][0]++;
    if ((unsigned) result != old_result) {
        record[RESULT_OFFSET] = (uint8_t) result;
        stats->changed++;
        stats->by_pieces[pieces][1]++;
    }
}

typedef struct {
    uint8_t* records;
    size_t count;
    stats_t stats;
} job_t;

static void* relabel_job(void* arg) {
    job_t* job = arg;
    for (size_t i = 0; i < job->count; i++) relabel_record(job->records + i * RECORD, &job->stats);
    return NULL;
}

/* Relabels 'count' records in place on opt_threads threads, adding to 'stats'. */
static void relabel_block(uint8_t* records, size_t count, stats_t* stats) {
    job_t jobs[MAX_THREADS];
    pthread_t threads[MAX_THREADS];
    const size_t per = (count + opt_threads - 1) / opt_threads;
    unsigned started = 0;
    for (unsigned t = 0; t < opt_threads; t++) {
        const size_t first = t * per;
        if (first >= count) break;
        jobs[t] = (job_t) {records + first * RECORD, count - first < per ? count - first : per, {0}};
        if (pthread_create(&threads[t], NULL, relabel_job, &jobs[t]) != 0) {
            fprintf(stderr, "syzygy_relabel: can't start a thread\n");
            exit(1);
        }
        started++;
    }
    for (unsigned t = 0; t < started; t++) {
        pthread_join(threads[t], NULL);
        stats_add(stats, &jobs[t].stats);
    }
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void print_file_stats(const char* name, const stats_t* s, double seconds) {
    printf("%s: %llu records, %llu probed (%.2f%%), %llu changed (%.2f%% of probed); "
           "left unchanged: castling %llu, en passant %llu, failed %llu; %.0f s\n",
           name, (unsigned long long) s->records, (unsigned long long) s->probed,
           s->records ? 100.0 * s->probed / s->records : 0.0, (unsigned long long) s->changed,
           s->probed ? 100.0 * s->changed / s->probed : 0.0, (unsigned long long) s->castling,
           (unsigned long long) s->en_passant, (unsigned long long) s->failed, seconds);
    fflush(stdout);
}

/* Relabels one file into 'out' (NULL with --stats-only). Returns false if it was skipped. */
static bool process_file(const char* in, const char* out, stats_t* total) {
    if (out && !opt_force && access(out, F_OK) == 0) {
        printf("%s: %s exists, skipped (--force rewrites it)\n", in, out);
        return false;
    }
    FILE* input = fopen(in, "rb");
    if (!input) {
        fprintf(stderr, "syzygy_relabel: can't open %s: %s\n", in, strerror(errno));
        exit(1);
    }
    struct stat st;
    char magic[8];
    if (fstat(fileno(input), &st) != 0 || fread(magic, 1, 8, input) != 8 || memcmp(magic, MAGIC, 8) != 0) {
        fprintf(stderr, "syzygy_relabel: %s isn't TIKIBIN1 data\n", in);
        exit(1);
    }
    if ((st.st_size - 8) % RECORD != 0) {
        fprintf(stderr, "syzygy_relabel: %s isn't a whole number of records (truncated?)\n", in);
        exit(1);
    }
    const uint64_t records = (uint64_t) (st.st_size - 8) / RECORD;

    char* partial = NULL;
    FILE* output = NULL;
    if (out) {
        partial = malloc(strlen(out) + 6);
        sprintf(partial, "%s.part", out);
        output = fopen(partial, "wb");
        if (!output || fwrite(MAGIC, 1, 8, output) != 8) {
            fprintf(stderr, "syzygy_relabel: can't write %s: %s\n", partial, strerror(errno));
            exit(1);
        }
    }

    uint8_t* buffer = malloc((size_t) BLOCK_RECORDS * RECORD);
    if (!buffer) {
        fprintf(stderr, "syzygy_relabel: out of memory\n");
        exit(1);
    }
    stats_t stats = {0};
    const double start = now();
    double last_report = start;
    uint64_t done = 0;
    while (done < records) {
        const size_t want = records - done < BLOCK_RECORDS ? (size_t) (records - done) : BLOCK_RECORDS;
        if (fread(buffer, RECORD, want, input) != want) {
            fprintf(stderr, "syzygy_relabel: read error in %s\n", in);
            exit(1);
        }
        relabel_block(buffer, want, &stats);
        if (output && fwrite(buffer, RECORD, want, output) != want) {
            fprintf(stderr, "syzygy_relabel: write error in %s: %s\n", partial, strerror(errno));
            exit(1);
        }
        done += want;
        if (now() - last_report >= 10.0) {
            last_report = now();
            fprintf(stderr, "  %s: %.1f%%, %llu probed, %llu changed\n", in, 100.0 * done / records,
                    (unsigned long long) stats.probed, (unsigned long long) stats.changed);
        }
    }
    free(buffer);
    fclose(input);
    if (output) {
        if (fclose(output) != 0 || rename(partial, out) != 0) {
            fprintf(stderr, "syzygy_relabel: can't finish %s: %s\n", out, strerror(errno));
            exit(1);
        }
        free(partial);
    }
    print_file_stats(in, &stats, now() - start);
    stats_add(total, &stats);
    return true;
}

static int compare_names(const void* a, const void* b) {
    return strcmp(*(char* const*) a, *(char* const*) b);
}

static bool is_directory(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static char* join(const char* dir, const char* name) {
    char* path = malloc(strlen(dir) + strlen(name) + 2);
    sprintf(path, "%s/%s", dir, name);
    return path;
}

static const char* base_name(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void print_totals(const stats_t* s, unsigned files) {
    printf("\ntotal (%u file%s): %llu records, %llu with at most %u pieces (%.2f%%)\n", files, files == 1 ? "" : "s",
           (unsigned long long) s->records, (unsigned long long) s->covered, opt_max_pieces,
           s->records ? 100.0 * s->covered / s->records : 0.0);
    printf("  probed %llu, changed %llu (%.2f%% of probed); left unchanged: castling %llu, en passant %llu, failed %llu\n",
           (unsigned long long) s->probed, (unsigned long long) s->changed,
           s->probed ? 100.0 * s->changed / s->probed : 0.0, (unsigned long long) s->castling,
           (unsigned long long) s->en_passant, (unsigned long long) s->failed);
    printf("\n  game result vs tablebase (probed positions, side to move):\n");
    printf("  %-10s %14s %14s %14s\n", "", "tb loss", "tb draw", "tb win");
    static const char* names[3] = {"game loss", "game draw", "game win"};
    for (int g = 0; g < 3; g++)
        printf("  %-10s %14llu %14llu %14llu\n", names[g], (unsigned long long) s->matrix[g][0],
               (unsigned long long) s->matrix[g][1], (unsigned long long) s->matrix[g][2]);
    printf("\n  %-7s %14s %14s %9s\n", "pieces", "probed", "changed", "changed%");
    for (int n = 2; n <= SLOTS; n++)
        if (s->by_pieces[n][0])
            printf("  %-7d %14llu %14llu %8.2f%%\n", n, (unsigned long long) s->by_pieces[n][0],
                   (unsigned long long) s->by_pieces[n][1], 100.0 * s->by_pieces[n][1] / s->by_pieces[n][0]);
    /* One line for scripts (tests/compare.py). */
    printf("\nstats: records=%llu covered=%llu castling=%llu en_passant=%llu failed=%llu probed=%llu changed=%llu\n",
           (unsigned long long) s->records, (unsigned long long) s->covered, (unsigned long long) s->castling,
           (unsigned long long) s->en_passant, (unsigned long long) s->failed, (unsigned long long) s->probed,
           (unsigned long long) s->changed);
}

static void usage(void) {
    fprintf(stderr,
            "usage: syzygy_relabel --tb DIR [options] IN OUT\n"
            "       syzygy_relabel --tb DIR --stats-only IN\n"
            "\n"
            "IN and OUT are TIKIBIN1 files, or directories (every *.bin in IN)\n"
            "\n"
            "  --tb DIR          Syzygy WDL tables (.rtbw); several directories separated by ':'\n"
            "  --max-pieces N    relabel positions with at most N pieces (default: the largest tables found)\n"
            "  --threads N       probing threads (default 4)\n"
            "  --stats-only      count what would change, write nothing\n"
            "  --force           rewrite outputs that already exist\n");
    exit(2);
}

int main(int argc, char** argv) {
    const char* tb = NULL;
    const char* paths[2] = {NULL, NULL};
    int path_count = 0;
    int max_pieces = -1;
    opt_threads = 4;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (!strcmp(a, "--tb") && i + 1 < argc) tb = argv[++i];
        else if (!strcmp(a, "--max-pieces") && i + 1 < argc) max_pieces = atoi(argv[++i]);
        else if (!strcmp(a, "--threads") && i + 1 < argc) opt_threads = (unsigned) atoi(argv[++i]);
        else if (!strcmp(a, "--stats-only")) opt_stats_only = true;
        else if (!strcmp(a, "--force")) opt_force = true;
        else if (a[0] == '-' && a[1] == '-') usage();
        else if (path_count < 2) paths[path_count++] = a;
        else usage();
    }
    if (!tb || path_count != (opt_stats_only ? 1 : 2) || opt_threads < 1 || opt_threads > MAX_THREADS) usage();

    if (!tb_init(tb) || TB_LARGEST == 0) {
        fprintf(stderr, "syzygy_relabel: no Syzygy tables found in %s\n", tb);
        return 1;
    }
    if (max_pieces < 0) opt_max_pieces = TB_LARGEST;
    else if ((unsigned) max_pieces > TB_LARGEST) {
        fprintf(stderr, "syzygy_relabel: --max-pieces %d, but the tables found go up to %u pieces\n", max_pieces, TB_LARGEST);
        return 1;
    } else opt_max_pieces = (unsigned) max_pieces;
    printf("tables: up to %u pieces (%s), relabelling positions with at most %u\n", TB_LARGEST, tb, opt_max_pieces);

    const char* in = paths[0];
    const char* out = opt_stats_only ? NULL : paths[1];
    if (out) {
        char real_in[4096], real_out[4096];
        if (realpath(in, real_in) && realpath(out, real_out) && !strcmp(real_in, real_out)) {
            fprintf(stderr, "syzygy_relabel: IN and OUT are the same; write the relabelled data elsewhere\n");
            return 1;
        }
    }
    stats_t total = {0};
    unsigned files = 0;

    if (is_directory(in)) {
        if (out && !is_directory(out) && mkdir(out, 0777) != 0) {
            fprintf(stderr, "syzygy_relabel: can't create %s: %s\n", out, strerror(errno));
            return 1;
        }
        DIR* dir = opendir(in);
        if (!dir) {
            fprintf(stderr, "syzygy_relabel: can't read %s: %s\n", in, strerror(errno));
            return 1;
        }
        char** names = NULL;
        size_t count = 0, capacity = 0;
        for (struct dirent* entry; (entry = readdir(dir));) {
            const size_t len = strlen(entry->d_name);
            if (len < 5 || strcmp(entry->d_name + len - 4, ".bin") != 0) continue;
            if (count == capacity) names = realloc(names, (capacity = capacity ? 2 * capacity : 64) * sizeof(char*));
            names[count++] = strdup(entry->d_name);
        }
        closedir(dir);
        qsort(names, count, sizeof(char*), compare_names);
        if (count == 0) {
            fprintf(stderr, "syzygy_relabel: no .bin files in %s\n", in);
            return 1;
        }
        for (size_t i = 0; i < count; i++) {
            char* source = join(in, names[i]);
            char* target = out ? join(out, names[i]) : NULL;
            files += process_file(source, target, &total);
            free(source);
            free(target);
            free(names[i]);
        }
        free(names);
    } else {
        char* target = NULL;
        if (out) target = is_directory(out) ? join(out, base_name(in)) : strdup(out);
        files += process_file(in, target, &total);
        free(target);
    }
    print_totals(&total, files);
    tb_free();
    return 0;
}
