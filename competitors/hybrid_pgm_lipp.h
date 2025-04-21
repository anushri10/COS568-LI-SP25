#pragma once
#include "dynamic_pgm_index.h"
#include "lipp.h"
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <atomic>
#include <vector>


template <class KeyType, class SearchClass, size_t pgm_error>
class HybridPGMLippAsync {
public:
    // flush_threshold: num of buff inserts before a flush
    HybridPGMLippAsync(size_t flush_threshold)
      : flush_threshold_(flush_threshold), stop_flag_(false)
    {
        // launch flush thread
        worker_ = std::thread(&HybridPGMLippAsync::flush_worker, this);
    }

    ~HybridPGMLippAsync() {
        // stop sign
        {
            std::lock_guard<std::mutex> lk(buffer_mutex_);
            stop_flag_ = true;
        }
        cv_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

    // Build both indices from initial data
    uint64_t Build(const std::vector<KeyValue<KeyType>>& data, size_t num_threads) {
        // Build DynamicPGM synchronously
        uint64_t t1 = dynamic_pgm_.Build(data, num_threads);
        // Bulk-load LIPP synchronously for initial data
        std::vector<std::pair<KeyType, uint64_t>> load;
        load.reserve(data.size());
        for (auto &kv : data) load.emplace_back(kv.key, kv.value);
        uint64_t t2 = util::timing([&] { lipp_.bulk_load(load.data(), load.size()); });
        return t1 + t2;
    }

    // Lookup: first DynamicPGM, then LIPP if not found
    size_t EqualityLookup(const KeyType& key, uint32_t thread_id) const {
        auto res = dynamic_pgm_.EqualityLookup(key, thread_id);
        if (res != util::OVERFLOW) return res;

        std::shared_lock<std::shared_mutex> lock(lipp_mutex_);
        uint64_t val;
        if (!lipp_.find(key, val)) return util::OVERFLOW;
        return val;
    }

    // Insert: buffer, dynamic insert, and trigger async flush when threshold reached
    void Insert(const KeyValue<KeyType>& kv, uint32_t thread_id) {
        // 1) insert to dynamic index
        dynamic_pgm_.Insert(kv, thread_id);

        // 2) buffer for async flush
        {
            std::lock_guard<std::mutex> lk(buffer_mutex_);
            buffer_.push_back(kv);
            if (buffer_.size() >= flush_threshold_) {
                // swap buffers and notify
                buffer_.swap(flush_buffer_);
                cv_.notify_one();
            }
        }
    }

private:
    // Background flush thread
    void flush_worker() {
        std::vector<KeyValue<KeyType>> to_flush;
        while (true) {
            {   // wait for work or stop
                std::unique_lock<std::mutex> lk(buffer_mutex_);
                cv_.wait(lk, [&] { return !flush_buffer_.empty() || stop_flag_; });
                if (stop_flag_ && flush_buffer_.empty()) break;
                // grab work
                to_flush.swap(flush_buffer_);
            }

            // bulk-insert into LIPP
            {
                std::unique_lock<std::shared_mutex> lock(lipp_mutex_);
                for (auto &kv : to_flush) {
                    lipp_.insert(kv.key, kv.value);
                }
            }
            to_flush.clear();
        }
    }

    // Indices
    DynamicPGM<KeyType, SearchClass, pgm_error> dynamic_pgm_{{}};
    Lipp<KeyType> lipp_{{}};

    // Async buffering
    size_t flush_threshold_;
    std::vector<KeyValue<KeyType>> buffer_;
    std::vector<KeyValue<KeyType>> flush_buffer_;
    std::mutex buffer_mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_flag_;

    // Protects LIPP during concurrent access
    mutable std::shared_mutex lipp_mutex_;
    std::thread worker_;
};
