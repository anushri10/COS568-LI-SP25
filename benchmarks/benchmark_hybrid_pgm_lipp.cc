// #include "benchmarks/benchmark_hybrid_pgm_lipp.h"
// #include "common.h"              // INSTANTIATE_TEMPLATES 
// #include "competitors/hybrid_pgm_lipp.h"     // our new competitor

// template <typename Searcher>
// void benchmark_64_hybrid_pgm_lipp(
//     tli::Benchmark<uint64_t>& benchmark,
//     bool /*pareto*/,
//     const std::vector<int>& params)
// {
//   // params[0] is flush_threshold
//   benchmark.template Run<
//     HybridPGMLippAsync<uint64_t, Searcher, 16>
//   >(params);
// }

// template <int record>
// void benchmark_64_hybrid_pgm_lipp(
//     tli::Benchmark<uint64_t>& benchmark,
//     const std::string&)
// {
//   // use the default ctor which supplies a default flush_threshold
//   std::vector<int> no_params;
//   benchmark.template Run<
//     HybridPGMLippAsync<uint64_t, BranchingBinarySearch<record>, 16>
//   >();
// }

// // instantiate for all the Searchers and the file‐based overload
// INSTANTIATE_TEMPLATES(benchmark_64_hybrid_pgm_lipp, uint64_t)

#include "benchmarks/benchmark_hybrid_pgm_lipp.h"
#include "common.h"                  // for INSTANTIATE_TEMPLATES
#include "competitors/hybrid_pgm_lipp.h"

template <typename Searcher>
void benchmark_64_hybrid_pgm_lipp(
    tli::Benchmark<uint64_t>& benchmark,
    bool /*pareto*/,
    const std::vector<int>& params)
{
  // params[0] -> flush_threshold
  benchmark.template Run<
    HybridPGMLippAsync<uint64_t, Searcher, 16>
  >(params);
}

template <int record>
void benchmark_64_hybrid_pgm_lipp(
    tli::Benchmark<uint64_t>& benchmark,
    const std::string& /*filename*/)
{
  // no_params => uses HybridPGMLippAsync(default_ctor) => default flush_threshold
  std::vector<int> no_params;
  benchmark.template Run<
    HybridPGMLippAsync<uint64_t, BranchingBinarySearch<record>, 16>
  >(no_params);
}

// Instantiate for all searchers and both overloads.
INSTANTIATE_TEMPLATES(benchmark_64_hybrid_pgm_lipp, uint64_t);
