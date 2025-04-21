#pragma once

#include "base.h"                  // for Competitor<>
#include "dynamic_pgm_index.h"     // for DynamicPGM<>
#include "lipp.h"                  // for Lipp<>
#include "util.h"                  // for util::timing, util::OVERFLOW
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <string>

template <class KeyType, class SearchClass, size_t pgm_error>
class HybridPGMLippAsync : public Competitor<KeyType, SearchClass> {
public:
  // Called by the harness: read first element as flush_threshold
  HybridPGMLippAsync(const std::vector<int>& params)
    : HybridPGMLippAsync(
        params.empty() ? static_cast<size_t>(100000)
                       : static_cast<size_t>(params[0])
      )
  {}

  // Internal constructor
  HybridPGMLippAsync(size_t flush_threshold)
    : flush_threshold_(flush_threshold),
      stop_flag_(false)
  {
    worker_ = std::thread(&HybridPGMLippAsync::flush_worker, this);
  }

  ~HybridPGMLippAsync() override {
    { 
      std::lock_guard<std::mutex> lk(buffer_mutex_);
      stop_flag_ = true;
    }
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
  }

  // Build initial index (called once, before file-based workload)
  uint64_t Build(const std::vector<KeyValue<KeyType>>& data,
                 size_t num_threads) override
  {
    // 1) DynamicPGM build
    uint64_t t1 = dynamic_pgm_.Build(data, num_threads);
    // 2) Bulk-load to LIPP
    std::vector<std::pair<KeyType,uint64_t>> load;
    load.reserve(data.size());
    for (auto &kv : data) load.emplace_back(kv.key, kv.value);
    uint64_t t2 = util::timing(
      [&]{ lipp_.bulk_load(load.data(), load.size()); }
    );
    return t1 + t2;
  }

  // Lookup
  size_t EqualityLookup(const KeyType& key,
                        uint32_t thread_id) const override
  {
    auto r = dynamic_pgm_.EqualityLookup(key, thread_id);
    if (r != util::OVERFLOW) return r;
    std::shared_lock<std::shared_mutex> lk(lipp_mutex_);
    uint64_t v;
    return lipp_.find(key, v) ? v : util::OVERFLOW;
  }

  // Insert
  void Insert(const KeyValue<KeyType>& kv,
              uint32_t thread_id) override
  {
    dynamic_pgm_.Insert(kv, thread_id);
    {
      std::lock_guard<std::mutex> lk(buffer_mutex_);
      buffer_.push_back(kv);
      if (buffer_.size() >= flush_threshold_) {
        buffer_.swap(flush_buffer_);
        cv_.notify_one();
      }
    }
  }

  // Metadata
  std::string name() const override { return "HybridPGM"; }
  std::size_t size() const override {
    return dynamic_pgm_.size() + lipp_.index_size();
  }
  bool applicable(bool unique,
                  bool range_query,
                  bool insert,
                  bool multithread,
                  const std::string& ops_filename) const override
  {
    // mirror DynamicPGM: no multithread
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
      std::unique_lock<std::shared_mutex> lk2(lipp_mutex_);
      for (auto &kv : to_flush) lipp_.insert(kv.key, kv.value);
      to_flush.clear();
    }
  }

  DynamicPGM<KeyType, SearchClass, pgm_error> dynamic_pgm_{{}};
  Lipp<KeyType>                               lipp_{{}};

  size_t                                  flush_threshold_;
  std::vector<KeyValue<KeyType>>          buffer_, flush_buffer_;
  std::mutex                              buffer_mutex_;
  std::condition_variable                 cv_;
  std::atomic<bool>                       stop_flag_;
  mutable std::shared_mutex               lipp_mutex_;
  std::thread                             worker_;
};
