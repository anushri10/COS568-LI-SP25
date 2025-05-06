#pragma once

#include "base.h"                  // Competitor<>
#include "dynamic_pgm_index.h"     // DynamicPGM<>
#include "lipp.h"                  // Lipp<>
#include "util.h"                  // util::OVERFLOW, util::timing

#include <vector>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>

template <class KeyType, class SearchClass, size_t pgm_error>
class HybridPGMLippAsync : public Competitor<KeyType, SearchClass> {
public:
  // Harness constructor: params[0] = initial flush_threshold
  HybridPGMLippAsync(const std::vector<int>& params)
    : HybridPGMLippAsync(
        params.empty() ? default_flush_threshold_ 
                       : static_cast<size_t>(params[0])
      )
  {}

  // Internal ctor
  HybridPGMLippAsync(size_t flush_threshold)
    : flush_threshold_(flush_threshold),
      stop_flag_(false),
      // adaptive tuning state
      min_threshold_(50'000),
      max_threshold_(2'000'000),
      avg_insert_rate_(0.0),
      avg_lookup_latency_(0.0),
      flush_interval_s_(1.0),
      insert_count_since_last_flush_(0),
      lookup_count_since_last_flush_(0),
      lookup_latency_ns_since_last_flush_(0),
      target_lookup_ns_(200.0),    // e.g. 200 ns target
      target_insert_mops_(1e3)     // e.g. 1 Mops/sec
  {
    worker_ = std::thread(&HybridPGMLippAsync::flush_worker, this);
  }

  ~HybridPGMLippAsync(){
    { std::lock_guard<std::mutex> lk(buffer_mutex_); stop_flag_ = true; }
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
  }

  // Build both indices
  uint64_t Build(const std::vector<KeyValue<KeyType>>& data,
                 size_t num_threads)
  {
    uint64_t t1 = dynamic_pgm_.Build(data, num_threads);
    uint64_t t2 = lipp_.Build(data, num_threads);
    return t1 + t2;
  }

  // Lookup: DPGM → LIPP, track latency
  size_t EqualityLookup(const KeyType& key, uint32_t thread_id) {
    auto r = dynamic_pgm_.EqualityLookup(key, thread_id);
    if (r != util::OVERFLOW) return r;

    auto start = std::chrono::high_resolution_clock::now();
    size_t v = lipp_.EqualityLookup(key, thread_id);
    auto end   = std::chrono::high_resolution_clock::now();

    uint64_t lat = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    lookup_count_since_last_flush_++;
    lookup_latency_ns_since_last_flush_ += lat;
    return v;
  }

  // Insert: DPGM + two‑level buffering → async flush
  void Insert(const KeyValue<KeyType>& kv, uint32_t thread_id) {
    dynamic_pgm_.Insert(kv, thread_id);
    insert_count_since_last_flush_++;

    // Stage 1
    buffer1_.push_back(kv);
    if (buffer1_.size() >= T1_) {
      std::sort(buffer1_.begin(), buffer1_.end(),
                [](auto &a, auto &b){ return a.key < b.key; });
      // merge into stage 2
      std::vector<KeyValue<KeyType>> merged;
      merged.reserve(buffer2_.size() + buffer1_.size());
      std::merge(buffer2_.begin(), buffer2_.end(),
                 buffer1_.begin(), buffer1_.end(),
                 std::back_inserter(merged),
                 [](auto &a, auto &b){ return a.key < b.key; });
      buffer2_.swap(merged);
      buffer1_.clear();

      if (buffer2_.size() >= T2_) {
        std::lock_guard<std::mutex> lk(buffer_mutex_);
        flush_buffer_.swap(buffer2_);
        cv_.notify_one();
      }
    }
  }

  // Combined size
  std::size_t size() const {
    return dynamic_pgm_.size() + lipp_.size();
  }

  std::string name() const { return "HybridPGM"; }
  bool applicable(bool unique, bool range_query, bool insert,
                  bool multithread, const std::string&) const
  {
    return !multithread;
  }

private:
  void flush_worker() {
    std::vector<KeyValue<KeyType>> to_flush;
    while (true) {
      { // wait for work or stop
        std::unique_lock<std::mutex> lk(buffer_mutex_);
        cv_.wait(lk, [&]{ return stop_flag_ || !flush_buffer_.empty(); });
        if (stop_flag_ && flush_buffer_.empty()) break;
        to_flush.swap(flush_buffer_);
      }

      // 1) sort & parallel‐insert into LIPP
      std::sort(to_flush.begin(), to_flush.end(),
                [](auto &a, auto &b){ return a.key < b.key; });

      size_t N = to_flush.size();
      unsigned P = std::thread::hardware_concurrency();
      size_t chunk = (N + P - 1) / P;
      std::vector<std::thread> threads;

      std::shared_lock<std::shared_mutex> lock(lipp_mutex_);
      for (unsigned i = 0; i < P; ++i) {
        size_t start = i*chunk, end = std::min(N, start+chunk);
        if (start >= end) break;
        threads.emplace_back([&,start,end](){
          for (size_t j = start; j < end; ++j)
            lipp_.Insert(to_flush[j], 0);
        });
      }
      for (auto &t : threads) t.join();

      // 2) adaptive threshold tuning
      {
        constexpr double α = 0.1;
        double now_ins_rate = double(insert_count_since_last_flush_) / flush_interval_s_;
        double now_lkp_lat  = double(lookup_latency_ns_since_last_flush_) 
                              / std::max<uint64_t>(1, lookup_count_since_last_flush_);

        avg_insert_rate_    = α*now_ins_rate    + (1-α)*avg_insert_rate_;
        avg_lookup_latency_ = α*now_lkp_lat     + (1-α)*avg_lookup_latency_;

        if (avg_lookup_latency_ > target_lookup_ns_) {
          flush_threshold_ = std::max(min_threshold_, flush_threshold_/2);
        } else if (avg_insert_rate_ > target_insert_mops_) {
          flush_threshold_ = std::min(max_threshold_, flush_threshold_*2);
        }

        // reset counters for next round
        insert_count_since_last_flush_       = 0;
        lookup_count_since_last_flush_       = 0;
        lookup_latency_ns_since_last_flush_  = 0;
      }

      to_flush.clear();
    }
  }

  // Indices
  DynamicPGM<KeyType, SearchClass, pgm_error> dynamic_pgm_;
  Lipp<KeyType>                               lipp_;

  // Two‐level buffering
  std::vector<KeyValue<KeyType>> buffer1_, buffer2_, flush_buffer_;
  static constexpr size_t T1_ = 100'000, T2_ = 500'000;

  // Flush thread
  size_t           flush_threshold_, default_flush_threshold_ = 100'000;
  std::mutex       buffer_mutex_;
  std::condition_variable cv_;
  std::atomic<bool> stop_flag_;
  std::thread      worker_;

  // Protect LIPP in parallel
  mutable std::shared_mutex lipp_mutex_;

  // Adaptive‐threshold state
  size_t   min_threshold_, max_threshold_;
  double   avg_insert_rate_, avg_lookup_latency_, flush_interval_s_;
  uint64_t insert_count_since_last_flush_,
           lookup_count_since_last_flush_,
           lookup_latency_ns_since_last_flush_;
  double   target_lookup_ns_, target_insert_mops_;
};
