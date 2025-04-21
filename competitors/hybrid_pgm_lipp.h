#pragma once

#include "base.h"                  // for Competitor<>
#include "dynamic_pgm_index.h"     // for DynamicPGM<>
#include "lipp.h"                  // for Lipp<>
#include "util.h"                  // for util::OVERFLOW
#include <vector>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <atomic>

template <class KeyType, class SearchClass, size_t pgm_error>
class HybridPGMLippAsync : public Competitor<KeyType, SearchClass> {
public:
  // harness constructor: params[0] = flush_threshold
  HybridPGMLippAsync(const std::vector<int>& params)
    : HybridPGMLippAsync(
        params.empty() ? static_cast<size_t>(100000)
                       : static_cast<size_t>(params[0])
      )
  {}

  // internal ctor: launches background flush thread
  HybridPGMLippAsync(size_t flush_threshold)
    : flush_threshold_(flush_threshold),
      stop_flag_(false)
  {
    worker_ = std::thread(&HybridPGMLippAsync::flush_worker, this);
  }

  ~HybridPGMLippAsync() {
    {
      std::lock_guard<std::mutex> lk(buffer_mutex_);
      stop_flag_ = true;
    }
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
  }

  // Build both indices on the initial data
  uint64_t Build(const std::vector<KeyValue<KeyType>>& data,
                 size_t num_threads)
  {
    uint64_t t1 = dynamic_pgm_.Build(data, num_threads);
    uint64_t t2 = lipp_.Build(data, num_threads);
    return t1 + t2;
  }

  // Lookup in DPGM, then fall back to LIPP
  size_t EqualityLookup(const KeyType& key,
                        uint32_t thread_id) const
  {
    auto r = dynamic_pgm_.EqualityLookup(key, thread_id);
    if (r != util::OVERFLOW) return r;
    return lipp_.EqualityLookup(key, thread_id);
  }

  // Insert into DPGM, buffer for async flush into LIPP
  void Insert(const KeyValue<KeyType>& kv,
              uint32_t thread_id)
  {
    dynamic_pgm_.Insert(kv, thread_id);
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

  // Name of the index
  std::string name() const { return "HybridPGM"; }

  // Mirror DynamicPGM’s applicability (no multithread)
  bool applicable(bool unique,
                  bool range_query,
                  bool insert,
                  bool multithread,
                  const std::string& ops_filename) const
  {
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
      for (auto &kv : to_flush) {
        lipp_.Insert(kv, /*thread_id=*/0);
      }
      to_flush.clear();
    }
  }

  // Underlying indices
  DynamicPGM<KeyType, SearchClass, pgm_error> dynamic_pgm_{{}};
  Lipp<KeyType>                               lipp_{{}};

  // Async buffering
  size_t                                  flush_threshold_;
  std::vector<KeyValue<KeyType>>          buffer_, flush_buffer_;
  std::mutex                              buffer_mutex_;
  std::condition_variable                 cv_;
  std::atomic<bool>                       stop_flag_;
  std::thread                             worker_;
};
