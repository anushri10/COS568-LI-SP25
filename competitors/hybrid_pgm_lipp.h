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
#include <chrono>
#include <algorithm>

template <class KeyType, class SearchClass, size_t pgm_error>
class HybridPGMLippAsync : public Competitor<KeyType, SearchClass> {
public:
  // Called by the harness: first element of params = initial flush_threshold
  HybridPGMLippAsync(const std::vector<int>& params)
    : HybridPGMLippAsync(
        params.empty() ? default_flush_threshold_
                       : static_cast<size_t>(params[0])
      )
  {}

  // Core constructor
  HybridPGMLippAsync(size_t flush_threshold)
    : flush_threshold_(flush_threshold),
      min_threshold_(1000),
      max_threshold_(1<<20),
      stop_flag_(false),
      avg_insert_rate_(0.0),
      avg_lookup_latency_(0.0),
      insert_count_since_flush_(0),
      lookup_count_since_flush_(0),
      lookup_latency_ns_since_flush_(0),
      last_flush_time_(std::chrono::steady_clock::now())
  {
    worker_ = std::thread(&HybridPGMLippAsync::flush_worker, this);
  }

  // Join background thread
  ~HybridPGMLippAsync(){
    {
      std::lock_guard<std::mutex> lk(buffer_mutex_);
      stop_flag_ = true;
    }
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
  }

  // Bulk‐build both indexes
  uint64_t Build(const std::vector<KeyValue<KeyType>>& data,
                 size_t num_threads)
  {
    uint64_t t1 = dynamic_pgm_.Build(data, num_threads);
    uint64_t t2 = lipp_.Build(data, num_threads);
    // reset our timing baseline
    last_flush_time_ = std::chrono::steady_clock::now();
    return t1 + t2;
  }

  // Look up: first DPGM, then LIPP; we time each lookup for EWMA
  size_t EqualityLookup(const KeyType& key,
                        uint32_t thread_id)
  {
    auto t0 = std::chrono::steady_clock::now();
    size_t r = dynamic_pgm_.EqualityLookup(key, thread_id);
    auto t1 = std::chrono::steady_clock::now();
    lookup_latency_ns_since_flush_ += 
      std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    lookup_count_since_flush_ ++;
    if (r != util::OVERFLOW) return r;

    // miss in DPGM, fall back
    std::shared_lock<std::shared_mutex> lk(lipp_mutex_);
    return lipp_.EqualityLookup(key, thread_id);
  }

  // Insert into DPGM immediately, buffer for async flush into LIPP
  void Insert(const KeyValue<KeyType>& kv,
              uint32_t thread_id)
  {
    dynamic_pgm_.Insert(kv, thread_id);
    insert_count_since_flush_ ++;
    {
      std::lock_guard<std::mutex> lk(buffer_mutex_);
      buffer_.push_back(kv);
      if (buffer_.size() >= flush_threshold_) {
        flush_buffer_.swap(buffer_);
        cv_.notify_one();
      }
    }
  }

  // Combined memory footprint
  std::size_t size() const {
    return dynamic_pgm_.size() + lipp_.size();
  }

  std::string name() const { return "HybridPGM"; }

  bool applicable(bool unique,
                  bool range_query,
                  bool insert,
                  bool multithread,
                  const std::string& ops_filename) const
  {
    // same as DPGM: no multithreading
    return !multithread;
  }

private:
  void flush_worker() {
    std::vector<KeyValue<KeyType>> to_flush;
    while (true) {
      {
        std::unique_lock<std::mutex> lk(buffer_mutex_);
        cv_.wait(lk, [&]{ return stop_flag_ || !flush_buffer_.empty(); });
        if (stop_flag_ && flush_buffer_.empty()) break;
        to_flush.swap(flush_buffer_);
      }
      // Bulk‐insert into LIPP under write lock
      {
        std::unique_lock<std::shared_mutex> lk(lipp_mutex_);
        for (auto &kv : to_flush) {
          lipp_.Insert(kv, /*thread_id*/0);
        }
      }
      to_flush.clear();

      // --- adaptive flush‐threshold update ---
      using clk = std::chrono::steady_clock;
      auto now = clk::now();
      double interval_s = std::chrono::duration<double>(now - last_flush_time_).count();
      last_flush_time_ = now;

      // compute instantaneous rates
      double inst_ins_rate = insert_count_since_flush_ / interval_s;                        // ops/s
      double inst_lkp_lat  = double(lookup_latency_ns_since_flush_) / lookup_count_since_flush_; // ns/op

      // EWMA update
      avg_insert_rate_    = alpha * inst_ins_rate    + (1.0 - alpha) * avg_insert_rate_;
      avg_lookup_latency_ = alpha * inst_lkp_lat     + (1.0 - alpha) * avg_lookup_latency_;

      // adjust threshold
      if (avg_lookup_latency_ > target_lookup_ns_) {
        flush_threshold_ = std::max(min_threshold_, flush_threshold_ / 2);
      } else if (avg_insert_rate_ > target_insert_mops_) {
        flush_threshold_ = std::min(max_threshold_, flush_threshold_ * 2);
      }

      // reset counters
      insert_count_since_flush_        = 0;
      lookup_count_since_flush_        = 0;
      lookup_latency_ns_since_flush_   = 0;
    }
  }

  // Underlying indexes
  DynamicPGM<KeyType, SearchClass, pgm_error> dynamic_pgm_{{}};
  Lipp<KeyType>                               lipp_{{}};

  // Async buffering
  size_t                                  flush_threshold_;
  const size_t                            min_threshold_, max_threshold_;
  std::vector<KeyValue<KeyType>>          buffer_, flush_buffer_;
  std::mutex                              buffer_mutex_;
  std::condition_variable                 cv_;
  std::atomic<bool>                       stop_flag_;

  // Protects LIPP
  std::shared_mutex                       lipp_mutex_;
  std::thread                             worker_;

  // Adaptive‐threshold state
  double                                  avg_insert_rate_;
  double                                  avg_lookup_latency_;
  size_t                                  insert_count_since_flush_;
  size_t                                  lookup_count_since_flush_;
  uint64_t                                lookup_latency_ns_since_flush_;
  std::chrono::steady_clock::time_point   last_flush_time_;

  // Targets & EWMA weight
  static constexpr double                 alpha = 0.1;
  static constexpr double                 target_lookup_ns_    = 200.0;   // e.g. aim for ≤200 ns/lookups
  static constexpr double                 target_insert_mops_  = 5e6;     // e.g. 5 Mops/s
  static constexpr size_t                 default_flush_threshold_ = 100000;
};
