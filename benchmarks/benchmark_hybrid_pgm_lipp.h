#pragma once
#include "benchmark.h"
template <typename Searcher>
void benchmark_64_hybrid_pgm_lipp(tli::Benchmark<uint64_t>&, bool, const std::vector<int>&);
template <int record>
void benchmark_64_hybrid_pgm_lipp(tli::Benchmark<uint64_t>&, const std::string&);
