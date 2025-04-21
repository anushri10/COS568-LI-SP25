#include "benchmarks/benchmark_hybrid_pgm_lipp.h"
#include "benchmark.h"
#include "benchmarks/common.h"
#include "competitors/hybrid_pgm_lipp.h"

using namespace tli;

//------------------------------------------------------------------------------
// Build‐time registration: forwards params[0] (from --value) into your hybrid
//------------------------------------------------------------------------------
template <typename Searcher>
void benchmark_64_hybrid_pgm_lipp(
    tli::Benchmark<uint64_t>& benchmark,
    bool pareto,
    const std::vector<int>& params)
{
  // params[0] == flush_threshold
  benchmark.template Run<
    HybridPGMLippAsync<uint64_t, Searcher, /*pgm_error=*/16>
  >(params);
}

//------------------------------------------------------------------------------
// File‐based runner: exactly mirroring your other benchmarks
//------------------------------------------------------------------------------
template <int record>
void benchmark_64_hybrid_pgm_lipp(
    tli::Benchmark<uint64_t>& benchmark,
    const std::string& filename)
{
  benchmark.template Run<
    HybridPGMLippAsync<uint64_t, BranchingBinarySearch<record>, 16>
  >(filename);

  benchmark.template Run<
    HybridPGMLippAsync<uint64_t, LinearSearch<record>, 16>
  >(filename);

  benchmark.template Run<
    HybridPGMLippAsync<uint64_t, InterpolationSearch<record>, 16>
  >(filename);

  benchmark.template Run<
    HybridPGMLippAsync<uint64_t, ExponentialSearch<record>, 16>
  >(filename);
}

//------------------------------------------------------------------------------
// Instantiate for all of your usual searchers (track_errors = 0,1)
//------------------------------------------------------------------------------
INSTANTIATE_TEMPLATES(benchmark_64_hybrid_pgm_lipp, uint64_t);