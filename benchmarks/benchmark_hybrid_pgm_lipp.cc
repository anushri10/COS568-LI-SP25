#include "benchmarks/benchmark_hybrid_pgm_lipp.h"
#include "benchmark.h"
#include "benchmarks/common.h"
#include "competitors/hybrid_pgm_lipp.h"

using namespace tli;

// Build-time registration (sweeps flush_threshold via params[0])
template <typename Searcher>
void benchmark_64_hybrid_pgm_lipp(
    tli::Benchmark<uint64_t>& benchmark,
    bool pareto,
    const std::vector<int>& /*params*/)
{
    if (!pareto) {
        util::fail("HybridPGM's hyperparameter cannot be set");
    } else {
        // Competitor ctor will read params[0] for flush_threshold
        benchmark.template Run<
          HybridPGMLippAsync<uint64_t, Searcher, /*pgm_error=*/16>
        >();
    }
}

// File-based runner (called for each workload file)
template <int record>
void benchmark_64_hybrid_pgm_lipp(
    tli::Benchmark<uint64_t>& benchmark,
    const std::string& /*filename*/)
{
    benchmark.template Run<
      HybridPGMLippAsync<uint64_t, BranchingBinarySearch<record>,16>
    >();
    benchmark.template Run<
      HybridPGMLippAsync<uint64_t, LinearSearch<record>,16>
    >();
    benchmark.template Run<
      HybridPGMLippAsync<uint64_t, InterpolationSearch<record>,16>
    >();
    benchmark.template Run<
      HybridPGMLippAsync<uint64_t, ExponentialSearch<record>,16>
    >();
}

INSTANTIATE_TEMPLATES_MULTITHREAD(benchmark_64_hybrid_pgm_lipp, uint64_t);