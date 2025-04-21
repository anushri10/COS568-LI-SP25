#pragma once
#include "base.h"                  // Competitor<>
#include "dynamic_pgm_index.h"     // DynamicPGM<>
#include "lipp.h"                  // Lipp<>
#include "util.h"                  // util::OVERFLOW, util::NOT_FOUND
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
  // Harness constructor: params[0] = flush threshold
  HybridPGMLippAsync(const std::vector<int>& params)
    : HybridPGMLippAsync(
        params.empty() ? static_cast<size_t>(100000)
                       : static_cast<size_t>(params[0])
      )
  {}

  // Real constructor: starts background flush thread
  HybridPGMLippAsync(size_t flush_threshold)
    : flush_threshold_(flush_threshold),
      stop_flag_(false)
  {
    worker_ = std::thread(&HybridPGMLippAsync::flush_worker, this);
  }

  // Join background thread
  ~HybridPGMLippAsync() {
    {
      std::lock_guard<std::mutex> lk(buffer_mutex_);
      stop_flag_ = true;
    }
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
  }

  // 1) Build initial data: DynamicPGM + LIPP via wrapper
  uint64_t Build(const std::vector<KeyValue<KeyType>>& data,
                 size_t num_threads)
  {
    uint64_t t1 = dynamic_pgm_.Build(data, num_threads);
    uint64_t t2 = lipp_.Build(data, num_threads);
    return t1 + t2;
  }

  // 2) Lookup: try DynamicPGM, then wrapper’s EqualityLookup
  size_t EqualityLookup(const KeyType& key,
                        uint32_t thread_id) const
  {
    auto r = dynamic_pgm_.EqualityLookup(key, thread_id);
    if (r != util::OVERFLOW) return r;

    std::shared_lock<std::shared_mutex> lk(lipp_mutex_);
    size_t r2 = lipp_.EqualityLookup(key, thread_id);
    return (r2 == util::NOT_FOUND ? util::OVERFLOW : r2);
  }

  // 3) Insert: immediate into DynamicPGM, buffered→async into LIPP
  void Insert(const KeyValue<KeyType>& kv,
              uint32_t thread_id)
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

  // 4) Metadata
  std::string name() const { return "HybridPGM"; }
  std::size_t size() const {
    return dynamic_pgm_.size() + lipp_.size();
  }
  bool applicable(bool unique, bool range_query,
                  bool insert, bool multithread,
                  const std::string&) const
  {
    return !multithread;
  }

private:
  // Background flush: drain flush_buffer_ → wrapper’s Insert
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
      for (auto &kv : to_flush) {
        lipp_.Insert(kv, /*thread_id=*/0);
      }
      to_flush.clear();
    }
  }

  // Components
  DynamicPGM<KeyType, SearchClass, pgm_error> dynamic_pgm_{{}};
  Lipp<KeyType>                               lipp_{{}};

  // Buffering
  size_t                                     flush_threshold_;
  std::vector<KeyValue<KeyType>>             buffer_, flush_buffer_;
  std::mutex                                 buffer_mutex_;
  std::condition_variable                    cv_;
  std::atomic<bool>                          stop_flag_;

  // Protects LIPP during reads/inserts
  mutable std::shared_mutex                  lipp_mutex_;
  std::thread                                worker_;
};