#include "benchmark.h"
#include "benchmark_btree.h"
#include "benchmark_dynamic_pgm.h"
#include "benchmark_lipp.h"
#include "benchmark_hybrid_pgm_lipp.h"
#include "common.h"

#include "cxxopts.hpp"

int main(int argc, char* argv[]) {
  try {
    cxxopts::Options options(argv[0], "Learned‑index throughput/insertion/lookup benchmark");
    options
      .positional_help("data ops")
      .show_positional_help();

    options.add_options()
      ("h,help",       "Print help")
      ("data",         "Dataset filename",       cxxopts::value<std::string>())
      ("ops",          "Operations filename",    cxxopts::value<std::string>())
      ("only",         "Which index to run (BTree, DynamicPGM, LIPP, HybridPGM)",
                       cxxopts::value<std::string>()->default_value(""))
      ("through",      "Throughput mode",        cxxopts::value<bool>()->default_value("false"))
      ("build",        "Only build index",       cxxopts::value<bool>()->default_value("false"))
      ("fence",        "Memory fence each op",   cxxopts::value<bool>()->default_value("false"))
      ("cold_cache",   "Clear cache each op",    cxxopts::value<bool>()->default_value("false"))
      ("errors",       "Track bounds/errors",    cxxopts::value<bool>()->default_value("false"))
      ("csv",          "Output CSV",             cxxopts::value<bool>()->default_value("false"))
      ("threads",      "Number of threads",      cxxopts::value<size_t>()->default_value("1"))
      ("repeats,r",    "Repeats per experiment", cxxopts::value<size_t>()->default_value("1"))
      ("verify",       "Verify correctness",     cxxopts::value<bool>()->default_value("false"))
      ("value",        "Index hyperparameter (e.g. flush‑threshold)",
                       cxxopts::value<int>()->default_value("0"))
    ;

    options.parse_positional({"data","ops"});
    auto result = options.parse(argc, argv);

    if (result.count("help") ||
        !result.count("data") ||
        !result.count("ops"))
    {
      std::cout << options.help({""}) << std::endl;
      return 0;
    }

    // Required args
    std::string data_file = result["data"].as<std::string>();
    std::string ops_file  = result["ops"].as<std::string>();

    // Flags
    bool through    = result["through"].as<bool>();
    bool build_only = result["build"].as<bool>();
    bool fence      = result["fence"].as<bool>();
    bool cold_cache = result["cold_cache"].as<bool>();
    bool track_err  = result["errors"].as<bool>();
    bool csv        = result["csv"].as<bool>();
    size_t threads  = result["threads"].as<size_t>();
    size_t repeats  = result["repeats"].as<size_t>();
    bool verify     = result["verify"].as<bool>();
    std::string only = result["only"].as<std::string>();

    // Hyperparameter
    int hyper = result["value"].as<int>();
    std::vector<int> params;
    if (hyper > 0) {
      params.push_back(hyper);
    }

    // Construct the harness
    tli::Benchmark<uint64_t> bench(
      data_file,
      ops_file,
      repeats,
      through,
      build_only,
      fence,
      cold_cache,
      track_err,
      csv,
      threads,
      verify
    );

    bool pareto = false;  // not used in most benchmarks

    // Dispatch to the selected index (or all, if --only is empty)
    if (only.empty() || only == "BTree") {
      benchmark_64_btree<BranchingBinarySearch<0>>(bench, pareto, params);
    }
    if (only.empty() || only == "DynamicPGM") {
      benchmark_64_dynamic_pgm<BranchingBinarySearch<0>>(bench, pareto, params);
    }
    if (only.empty() || only == "LIPP") {
      // LIPP has no hyperparameter or multiple searchers
      benchmark_64_lipp(bench);
    }
    if (only.empty() || only == "HybridPGM") {
      benchmark_64_hybrid_pgm_lipp<BranchingBinarySearch<0>>(bench, pareto, params);
    }

  } catch (const cxxopts::OptionException &e) {
    std::cerr << "Error parsing options: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}