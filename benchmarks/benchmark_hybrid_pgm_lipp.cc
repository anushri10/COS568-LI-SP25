// benchmarks/benchmark_hybrid_pgm_lipp.cc

#include "benchmark_hybrid_pgm_lipp.h"
#include "../competitors/hybrid_pgm_lipp.h"

using namespace tli;

//-----------------------------------------------------------------------------
// 1) Build‑time registration: sweep `--params` (used here for flush_threshold)
//-----------------------------------------------------------------------------
template <typename Searcher>
void benchmark_64_hybrid_pgm_lipp(
    tli::Benchmark<uint64_t>& bench,
    bool pareto,
    const std::vector<int>& params)
{
    bench.registerCompetitor<
        HybridPGMLippAsync<uint64_t, Searcher, /* pgm_error = */ 16>
    >("HybridPGM", params, pareto);
}

//-----------------------------------------------------------------------------
// 2) File‑based runner: fixed Searcher = LinearSearch<record>, pgm_error = 16
//-----------------------------------------------------------------------------
template <int record>
void benchmark_64_hybrid_pgm_lipp(
    tli::Benchmark<uint64_t>& bench,
    const std::string& filename)
{
    bench.runFromFile<
        HybridPGMLippAsync<uint64_t, LinearSearch<record>, /* pgm_error = */ 16>
    >(filename);
}

//-----------------------------------------------------------------------------
// 3) Explicit instantiation for multithreaded benchmarks
//    (uses the same macro as your other `benchmark_*.cc` files)
//-----------------------------------------------------------------------------
INSTANTIATE_TEMPLATES_MULTITHREAD(benchmark_64_hybrid_pgm_lipp, uint64_t);
