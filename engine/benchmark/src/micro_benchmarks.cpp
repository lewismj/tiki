#include <benchmark/benchmark.h>

#include "tiki_shim.h"

/**
 * Micro-benchmarks for the parts the evaluation swap touches (eval, and the search around it).
 * Each benchmark runs over these positions, selected by the argument.
 */
static const char* const positions[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 10",
        "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 11",
};
static const int position_count = sizeof(positions) / sizeof(positions[0]);


static tiki_position* position_for(benchmark::State& state) {
    return tiki_position_new(positions[state.range(0)]);
}

static void BM_Evaluate(benchmark::State& state) {
    tiki_position* position = position_for(state);
    for (auto _ : state) benchmark::DoNotOptimize(tiki_evaluate(position));
    tiki_position_free(position);
}

static void BM_GenerateMoves(benchmark::State& state) {
    tiki_position* position = position_for(state);
    int64_t moves = 0;
    for (auto _ : state) moves += tiki_generate_moves(position);
    state.counters["moves/s"] = benchmark::Counter((double) moves, benchmark::Counter::kIsRate);
    tiki_position_free(position);
}

static void BM_MakeUndo(benchmark::State& state) {
    tiki_position* position = position_for(state);
    int64_t moves = 0;
    for (auto _ : state) moves += tiki_make_undo_all(position);
    state.counters["moves/s"] = benchmark::Counter((double) moves, benchmark::Counter::kIsRate);
    tiki_position_free(position);
}

static void BM_Perft(benchmark::State& state) {
    tiki_position* position = position_for(state);
    uint64_t nodes = 0;
    for (auto _ : state) nodes += tiki_perft(position, 3);
    state.counters["nodes/s"] = benchmark::Counter((double) nodes, benchmark::Counter::kIsRate);
    tiki_position_free(position);
}

static void BM_Search(benchmark::State& state) {
    tiki_position* position = position_for(state);
    uint64_t nodes = 0;
    for (auto _ : state) nodes += tiki_search(position, 6);
    state.counters["nodes/s"] = benchmark::Counter((double) nodes, benchmark::Counter::kIsRate);
    tiki_position_free(position);
}

BENCHMARK(BM_Evaluate)->DenseRange(0, position_count - 1);
BENCHMARK(BM_GenerateMoves)->DenseRange(0, position_count - 1);
BENCHMARK(BM_MakeUndo)->DenseRange(0, position_count - 1);
BENCHMARK(BM_Perft)->DenseRange(0, position_count - 1)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Search)->DenseRange(0, position_count - 1)->Unit(benchmark::kMillisecond);


int main(int argc, char** argv) {
    tiki_init(16);
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}
