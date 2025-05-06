// #pragma once
// #include "benchmark.h"

// template <typename Searcher>
// void benchmark_64_hybrid_pgm_lipp(
//     tli::Benchmark<uint64_t>& benchmark,
//     bool pareto,
//     const std::vector<int>& params);

// template <int record>
// void benchmark_64_hybrid_pgm_lipp(
//     tli::Benchmark<uint64_t>& benchmark,
//     const std::string& filename);

#pragma once
#include "benchmark.h"

/// Benchmark entrypoints for our new HybridPGM+LIPP competitor.
/// params[0] is interpreted as the flush_threshold for the async buffer.
template <typename Searcher>
void benchmark_64_hybrid_pgm_lipp(
    tli::Benchmark<uint64_t>& benchmark,
    bool pareto,
    const std::vector<int>& params);

/// File‐based overload (uses default flush_threshold).
template <int record>
void benchmark_64_hybrid_pgm_lipp(
    tli::Benchmark<uint64_t>& benchmark,
    const std::string& filename);
