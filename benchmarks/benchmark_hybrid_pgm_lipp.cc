// benchmarks/benchmark_hybrid_pgm_lipp.cc

#include "benchmarks/benchmark_hybrid_pgm_lipp.h"
#include "benchmark.h"
#include "benchmarks/common.h"
#include "competitors/hybrid_pgm_lipp.h"

//-----------------------------------------------------------------------------
// Build‑time registration: same style as PGM and DynamicPGM
//-----------------------------------------------------------------------------
template <typename Searcher>
void benchmark_64_hybrid_pgm_lipp(
    tli::Benchmark<uint64_t>& benchmark,
    bool pareto,
    const std::vector<int>& /* params */)
{
  if (!pareto) {
    util::fail("HybridPGM's hyperparameter cannot be set");
  } else {
    // flush-threshold is picked up via the --flush-threshold CLI flag
    benchmark.template Run<
      HybridPGMLippAsync<uint64_t, Searcher, /*pgm_error=*/16>
    >();
  }
}

//-----------------------------------------------------------------------------
// File‑based runner: exactly like DynamicPGM’s version
//-----------------------------------------------------------------------------
template <int record>
void benchmark_64_hybrid_pgm_lipp(
    tli::Benchmark<uint64_t>& benchmark,
    const std::string& /*filename*/)
{
  benchmark.template Run<
    HybridPGMLippAsync<uint64_t, LinearSearch<record>, /*pgm_error=*/16>
  >();
}

//-----------------------------------------------------------------------------
// Instantiate for all your search‐strategies & error settings
//-----------------------------------------------------------------------------
INSTANTIATE_TEMPLATES_MULTITHREAD(benchmark_64_hybrid_pgm_lipp, uint64_t);
