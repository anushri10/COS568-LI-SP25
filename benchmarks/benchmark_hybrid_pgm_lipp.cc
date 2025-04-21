#include "benchmarks/benchmark_hybrid_pgm_lipp.h"
#include "benchmark.h"
#include "benchmarks/common.h"
#include "competitors/hybrid_pgm_lipp.h"

using namespace tli;

//-----------------------------------------------------------------------------
// 1) Build‑time registration: hyperparameter = flush_threshold
//-----------------------------------------------------------------------------
template <typename Searcher>
void benchmark_64_hybrid_pgm_lipp(
    tli::Benchmark<uint64_t>& benchmark,
    bool pareto,
    const std::vector<int>& params)
{
  if (!pareto) {
    util::fail("HybridPGM's hyperparameter cannot be set");
  } else {
    // single instantiation: uses params[0] as flush_threshold
    benchmark.template Run<
      HybridPGMLippAsync<uint64_t, Searcher, /*pgm_error=*/16>
    >(params);
  }
}

//-----------------------------------------------------------------------------
// 2) File‑based runner: dispatch per-record searchers (pgm_error=16 fixed)
//-----------------------------------------------------------------------------
template <int record>
void benchmark_64_hybrid_pgm_lipp(
    tli::Benchmark<uint64_t>& benchmark,
    const std::string& filename)
{
  // run with the four core searchers
  benchmark.template Run<
    HybridPGMLippAsync<uint64_t, BranchingBinarySearch<record>,16>
  >(filename);

  benchmark.template Run<
    HybridPGMLippAsync<uint64_t, LinearSearch<record>,16>
  >(filename);

  benchmark.template Run<
    HybridPGMLippAsync<uint64_t, InterpolationSearch<record>,16>
  >(filename);

  benchmark.template Run<
    HybridPGMLippAsync<uint64_t, ExponentialSearch<record>,16>
  >(filename);
}

//-----------------------------------------------------------------------------
// 3) Instantiate for all record‐IDs & searchers
//-----------------------------------------------------------------------------
INSTANTIATE_TEMPLATES_MULTITHREAD(benchmark_64_hybrid_pgm_lipp, uint64_t);
