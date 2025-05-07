// #pragma once

// #include "base.h"                  // for Competitor<>
// #include "dynamic_pgm_index.h"     // for DynamicPGM<>
// #include "lipp.h"                  // for Lipp<>
// #include "util.h"                  // for util::OVERFLOW
// #include <vector>
// #include <thread>
// #include <mutex>
// #include <shared_mutex>
// #include <condition_variable>
// #include <atomic>
// #include <chrono>

// template <class KeyType, class SearchClass, size_t pgm_error>
// class HybridPGMLippAsync : public Competitor<KeyType,SearchClass> {
// public:
//   // Harness constructor: first param = initial flush_threshold
//   HybridPGMLippAsync(const std::vector<int>& params)
//     : HybridPGMLippAsync(
//         params.empty() ? static_cast<size_t>(100000)
//                        : static_cast<size_t>(params[0])
//       )
//   {}

//   // Internal ctor
//   HybridPGMLippAsync(size_t flush_threshold)
//     : flush_threshold_(flush_threshold),
//       stop_flag_(false),
//       // initialize EWMA state
//       avg_insert_rate_(0),
//       avg_lookup_latency_(0),
//       last_flush_time_(std::chrono::steady_clock::now()),
//       insert_count_since_last_flush_(0),
//       lookup_count_since_last_flush_(0),
//       lookup_latency_ns_since_last_flush_(0)
//   {
//     worker_ = std::thread(&HybridPGMLippAsync::flush_worker, this);
//   }

//   ~HybridPGMLippAsync(){
//     {
//       std::lock_guard<std::mutex> lk(buffer_mutex_);
//       stop_flag_ = true;
//     }
//     cv_.notify_one();
//     if (worker_.joinable()) worker_.join();
//   }

//   // Build both indices on the initial data
//   uint64_t Build(const std::vector<KeyValue<KeyType>>& data,
//                  size_t num_threads)
//   {
//     return dynamic_pgm_.Build(data, num_threads)
//          + lipp_.Build(data, num_threads);
//   }

//   // Lookup in DPGM, then fall back to LIPP, measuring latency
//   size_t EqualityLookup(const KeyType& key,
//                         uint32_t thread_id)
//   {
//     auto t0 = std::chrono::high_resolution_clock::now();

//     auto r = dynamic_pgm_.EqualityLookup(key, thread_id);
//     if (r == util::OVERFLOW)
//       r = lipp_.EqualityLookup(key, thread_id);

//     auto t1 = std::chrono::high_resolution_clock::now();
//     auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count();

//     // account for this lookup
//     ++lookup_count_since_last_flush_;
//     lookup_latency_ns_since_last_flush_ += ns;

//     return r;
//   }

//   // Insert into DPGM and buffer for async flush into LIPP
//   void Insert(const KeyValue<KeyType>& kv,
//               uint32_t thread_id)
//   {
//     dynamic_pgm_.Insert(kv, thread_id);
//     {
//       std::lock_guard<std::mutex> lk(buffer_mutex_);
//       buffer_.push_back(kv);
//     }
//     ++insert_count_since_last_flush_;
//     // wake worker if buffer big enough
//     if (buffer_.size() >= flush_threshold_) {
//       std::lock_guard<std::mutex> lk(buffer_mutex_);
//       buffer_.swap(flush_buffer_);
//       cv_.notify_one();
//     }
//   }

//   // Combined footprint
//   std::size_t size() const {
//     return dynamic_pgm_.size() + lipp_.size();
//   }

//   std::string name() const { return "HybridPGM"; }
//   bool applicable(bool unique, bool range_query,
//                   bool insert, bool multithread,
//                   const std::string& ops_filename) const
//   {
//     return !multithread;
//   }

// private:
//   // The background flush thread
//   void flush_worker() {
//     using clock = std::chrono::steady_clock;
//     std::vector<KeyValue<KeyType>> to_flush;

//     while (true) {
//       { // wait for work or stop
//         std::unique_lock<std::mutex> lk(buffer_mutex_);
//         cv_.wait(lk, [&]{ return stop_flag_ || !flush_buffer_.empty(); });
//         if (stop_flag_ && flush_buffer_.empty()) break;
//         to_flush.swap(flush_buffer_);
//       }

//       // bulk-insert into LIPP
//       for (auto &kv : to_flush) {
//         lipp_.Insert(kv, 0);
//       }
//       to_flush.clear();

//       // *** Adaptive threshold update ***
//       //
//       // measure interval
//       auto now = clock::now();
//       double dt_s = std::chrono::duration<double>(now - last_flush_time_).count();
//       last_flush_time_ = now;

//       // current rates
//       double now_ins_rate = insert_count_since_last_flush_ / dt_s;      // Mops/s
//       double now_lkp_lat = double(lookup_latency_ns_since_last_flush_) / lookup_count_since_last_flush_; // ns

//       // EWMA update
//       constexpr double α = 0.1;
//       avg_insert_rate_    = α * now_ins_rate    + (1-α) * avg_insert_rate_;
//       avg_lookup_latency_ = α * now_lkp_lat     + (1-α) * avg_lookup_latency_;

//       // reset counters
//       insert_count_since_last_flush_ = 0;
//       lookup_count_since_last_flush_ = 0;
//       lookup_latency_ns_since_last_flush_ = 0;

//       // adjust flush_threshold_
//       if (avg_lookup_latency_ > target_lookup_ns_) {
//         flush_threshold_ = std::max(min_threshold_, flush_threshold_/2);
//       } else if (avg_insert_rate_ > target_insert_mops_) {
//         flush_threshold_ = std::min(max_threshold_, flush_threshold_*2);
//       }
//       // *********************************
//     }
//   }

//   // Indices
//   DynamicPGM<KeyType,SearchClass,pgm_error> dynamic_pgm_{{}};
//   Lipp<KeyType>                             lipp_{{}};

//   // Adaptive flushing state
//   size_t                                  flush_threshold_;
//   const size_t                            min_threshold_   =  1000;
//   const size_t                            max_threshold_   = 1000000;
//   const double                            target_lookup_ns_=   200;   // tune to your #cores / workload
//   const double                            target_insert_mops_= 1e6;   // 1 M inserts/s

//   std::vector<KeyValue<KeyType>>          buffer_, flush_buffer_;
//   std::mutex                              buffer_mutex_;
//   std::condition_variable                 cv_;
//   std::atomic<bool>                       stop_flag_;
//   std::thread                             worker_;

//   // Metrics since last flush
//   clock::time_point                       last_flush_time_;
//   size_t                                  insert_count_since_last_flush_;
//   size_t                                  lookup_count_since_last_flush_;
//   uint64_t                                lookup_latency_ns_since_last_flush_;

//   // EWMA estimates
//   double                                  avg_insert_rate_;
//   double                                  avg_lookup_latency_;
// };


#pragma once

#include "dynamic_pgm_index.h"
#include "lipp.h"
#include "util.h"

#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <vector>
#include <cstdint>

template <class KeyType, class SearchClass, size_t pgm_error>
class HybridPGMLippAsync : public Competitor<KeyType, SearchClass> {
public:
  // params[0] = initial flush threshold (optional)
  explicit HybridPGMLippAsync(const std::vector<int>& params) {
    flush_threshold_ = params.empty() ? 100000
                                     : static_cast<size_t>(params[0]);
    last_flush_tp_ = clock_now();
    worker_ = std::thread(&HybridPGMLippAsync::flush_worker, this);
  }

  ~HybridPGMLippAsync() {
    { std::lock_guard<std::mutex> lk(buffer_mtx_); stop_ = true; }
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
  }

  // Build both indices on the initial data
  uint64_t Build(const std::vector<KeyValue<KeyType>>& data,
                 size_t num_threads) {
    uint64_t t1 = dpgm_.Build(data, num_threads);
    uint64_t t2 = util::timing([&] {
      std::vector<std::pair<KeyType,uint64_t>> load;
      load.reserve(data.size());
      for (auto &kv: data) load.emplace_back(kv.key, kv.value);
      lipp_.bulk_load(load.data(), load.size());
    });
    return t1 + t2;
  }

  // Lookup through DPGM, then fall back to LIPP
  size_t EqualityLookup(const KeyType& key, uint32_t tid) const {
    auto t0 = clock_now();
    size_t res = dpgm_.EqualityLookup(key, tid);
    if (res == util::OVERFLOW) {
      std::shared_lock<std::shared_mutex> lk(lipp_mtx_);
      uint64_t v;
      res = lipp_.find(key, v) ? v : util::OVERFLOW;
    }
    auto ns = since_ns(t0);
    lookup_lat_ns_.fetch_add(ns, std::memory_order_relaxed);
    lookup_cnt_.fetch_add(1, std::memory_order_relaxed);
    return res;
  }

  // Insert into DPGM + staging buffer for asynchronous flush
  void Insert(const KeyValue<KeyType>& kv, uint32_t /*tid*/) {
    dpgm_.Insert(kv, 0);
    insert_cnt_.fetch_add(1, std::memory_order_relaxed);

    {
      std::lock_guard<std::mutex> lk(buffer_mtx_);
      buffer_.push_back(kv);
    }
    if (buffer_.size() >= flush_threshold_) {
      cv_.notify_one();
    }
  }

  // Metadata
  std::string name() const { return "HybridPGM"; }
  std::size_t size() const {
    return dpgm_.size() + lipp_.index_size();
  }
  bool applicable(bool, bool, bool, bool multithread,
                  const std::string&) const {
    return !multithread;
  }

private:
  // Clock helpers
  using clk = std::chrono::steady_clock;
  static inline clk::time_point clock_now() {
    return clk::now();
  }
  static inline uint64_t since_ns(const clk::time_point& t0) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
      clk::now() - t0).count();
  }

  // Background flush + adaptive threshold thread
  void flush_worker() {
    std::vector<KeyValue<KeyType>> local;
    constexpr double alpha = 0.1;
    const uint64_t target_lookup_ns = 800;   // 0.8µs
    const double   target_insert_mops = 2.0; // 2 Mops/s

    while (true) {
      {
        std::unique_lock<std::mutex> lk(buffer_mtx_);
        cv_.wait_for(lk, std::chrono::milliseconds(10),
                     [&]{ return !buffer_.empty() || stop_; });
        if (stop_ && buffer_.empty()) break;
        buffer_.swap(local);
      }

      // Flush into LIPP
      {
        std::unique_lock<std::shared_mutex> lk(lipp_mtx_);
        for (auto &kv: local) lipp_.insert(kv.key, kv.value);
      }
      local.clear();

      // Compute instantaneous rates / latencies
      uint64_t lkp_cnt = lookup_cnt_.exchange(0);
      uint64_t lkp_lat = lookup_lat_ns_.exchange(0);
      uint64_t ins_cnt = insert_cnt_.exchange(0);

      double now_lat_ns  = lkp_cnt ? double(lkp_lat)/lkp_cnt : 0;
      double now_ins_mops =
        (ins_cnt/1e6) / flush_interval_sec();

      // Update EWMAs
      avg_lookup_lat_ns_ = alpha*now_lat_ns  + (1-alpha)*avg_lookup_lat_ns_;
      avg_insert_mops_   = alpha*now_ins_mops + (1-alpha)*avg_insert_mops_;

      // Adjust threshold
      if (avg_lookup_lat_ns_ > target_lookup_ns && flush_threshold_ > 10'000)
        flush_threshold_ /= 2;
      else if (avg_insert_mops_ > target_insert_mops && flush_threshold_ < 2'000'000)
        flush_threshold_ *= 2;

      last_flush_tp_ = clock_now();
    }
  }

  double flush_interval_sec() const {
    return std::chrono::duration<double>(
      clk::now() - last_flush_tp_).count();
  }

  // Indices
  DynamicPGM<KeyType, SearchClass, pgm_error> dpgm_{{}};
  Lipp<KeyType>                              lipp_{{}};

  // Staging buffer & sync
  std::vector<KeyValue<KeyType>> buffer_;
  mutable std::mutex             buffer_mtx_;
  mutable std::shared_mutex      lipp_mtx_;
  std::condition_variable        cv_;
  std::thread                    worker_;
  std::atomic<bool>              stop_{false};

  // Adaptive‐flush metrics
  mutable std::atomic<uint64_t> lookup_lat_ns_{0},
                                 lookup_cnt_{0},
                                 insert_cnt_{0};
  double avg_lookup_lat_ns_ = 0.0;
  double avg_insert_mops_   = 0.0;
  size_t flush_threshold_;
  clk::time_point last_flush_tp_;
};
