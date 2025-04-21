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

template <class KeyType, class SearchClass, size_t pgm_error>
class HybridPGMLippAsync : public Competitor<KeyType, SearchClass> {
public:
    // 1) Harness will call this (vector<int>), we take first entry as flush_threshold
    HybridPGMLippAsync(const std::vector<int>& params)
      : HybridPGMLippAsync(
          params.empty()
            ? static_cast<size_t>(100000)                    // default flush
            : static_cast<size_t>(params[0])
        )
    {}

    // 2) Real constructor used by us
    HybridPGMLippAsync(size_t flush_threshold)
      : flush_threshold_(flush_threshold), stop_flag_(false)
    {
        worker_ = std::thread(&HybridPGMLippAsync::flush_worker, this);
    }

    ~HybridPGMLippAsync() {
        // signal and join the background thread
        {
            std::lock_guard<std::mutex> lk(buffer_mutex_);
            stop_flag_ = true;
        }
        cv_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

    // Build both indices from initial data
    uint64_t Build(const std::vector<KeyValue<KeyType>>& data,
                   size_t num_threads) override
    {
        // 1) build DynamicPGM
        uint64_t t1 = dynamic_pgm_.Build(data, num_threads);

        // 2) bulk-load LIPP
        std::vector<std::pair<KeyType,uint64_t>> load;
        load.reserve(data.size());
        for (auto &kv : data) load.emplace_back(kv.key, kv.value);
        uint64_t t2 = util::timing(
          [&]{ lipp_.bulk_load(load.data(), load.size()); }
        );

        return t1 + t2;
    }

    // Lookup: first DynamicPGM, then LIPP if not found
    size_t EqualityLookup(const KeyType& key,
                          uint32_t thread_id) const override
    {
        auto res = dynamic_pgm_.EqualityLookup(key, thread_id);
        if (res != util::OVERFLOW) return res;

        std::shared_lock<std::shared_mutex> lock(lipp_mutex_);
        uint64_t val;
        if (!lipp_.find(key, val)) return util::OVERFLOW;
        return val;
    }

    // Insert: synchronous into DynamicPGM, asynchronous into LIPP
    void Insert(const KeyValue<KeyType>& kv,
                uint32_t thread_id) override
    {
        // 1) immediate insert to dynamic index
        dynamic_pgm_.Insert(kv, thread_id);

        // 2) buffer for async flush
        {
            std::lock_guard<std::mutex> lk(buffer_mutex_);
            buffer_.push_back(kv);
            if (buffer_.size() >= flush_threshold_) {
                buffer_.swap(flush_buffer_);
                cv_.notify_one();
            }
        }
    }

private:
    // Background thread: drains flush_buffer_ → inserts into LIPP
    void flush_worker() {
        std::vector<KeyValue<KeyType>> to_flush;
        while (true) {
            {
                std::unique_lock<std::mutex> lk(buffer_mutex_);
                cv_.wait(lk, [&]{ return stop_flag_ || !flush_buffer_.empty(); });
                if (stop_flag_ && flush_buffer_.empty())
                    break;
                to_flush.swap(flush_buffer_);
            }
            // perform the actual inserts
            {
                std::unique_lock<std::shared_mutex> lock(lipp_mutex_);
                for (auto &kv : to_flush) {
                    lipp_.insert(kv.key, kv.value);
                }
            }
            to_flush.clear();
        }
    }

    // components
    DynamicPGM<KeyType, SearchClass, pgm_error> dynamic_pgm_{{}};
    Lipp<KeyType>                               lipp_{{}};

    // async buffering
    size_t                      flush_threshold_;
    std::vector<KeyValue<KeyType>> buffer_;
    std::vector<KeyValue<KeyType>> flush_buffer_;
    std::mutex                  buffer_mutex_;
    std::condition_variable     cv_;
    std::atomic<bool>           stop_flag_;

    // guards LIPP during concurrent access
    mutable std::shared_mutex   lipp_mutex_;
    std::thread                 worker_;
};
