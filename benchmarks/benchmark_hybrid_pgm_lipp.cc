#include "benchmark_hybrid_pgm_lipp.h"
#include "../competitors/hybrid_pgm_lipp.h"

using namespace tli;

//-----------------------------------------------------------------------------
// 1) Build‑time registration: (we ignore params, just call Run once)
//-----------------------------------------------------------------------------
template <typename Searcher>
void benchmark_64_hybrid_pgm_lipp(
    tli::Benchmark<uint64_t>& benchmark,
    bool pareto,
    const std::vector<int>& /*params*/)
{
  if (!pareto) {
    util::fail("HybridPGM's hyperparameter cannot be set in this mode");
  } else {
    // single instantiation; any flush-threshold is picked up via the CLI flag
    benchmark.template Run<
      HybridPGMLippAsync<uint64_t, Searcher, /*pgm_error=*/16>
    >();
  }
}

//-----------------------------------------------------------------------------
// 2) File‑based runner: fixed Searcher = LinearSearch<record>, pgm_error = 16
//-----------------------------------------------------------------------------
template <int record>
void benchmark_64_hybrid_pgm_lipp(
    tli::Benchmark<uint64_t>& benchmark,
    const std::string& filename)
{
  benchmark.template RunFromFile<
    HybridPGMLippAsync<uint64_t, LinearSearch<record>, /*pgm_error=*/16>
  >(filename);
}

//-----------------------------------------------------------------------------
// 3) Macro to instantiate all the Searcher<> combinations (multithreaded mode)
//    Matches how your other benchmark_*.cc files do it.
//-----------------------------------------------------------------------------
INSTANTIATE_TEMPLATES_MULTITHREAD(benchmark_64_hybrid_pgm_lipp, uint64_t);
