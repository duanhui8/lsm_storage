/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/share/cache/ob_kv_storecache.h */

#pragma once

#include <unordered_map>
#include <list>
#include <mutex>
#include <functional>

namespace oceanbase {
namespace blocksstable {

/**
 * ObKVCache<Key, Value> — generic LRU key-value cache.
 * Simplified from OB 4.4.2 ObKVCache. No hazard pointers.
 */
template <typename Key, typename Value>
class ObKVCache
{
public:
  ObKVCache() : capacity_(1024), size_(0) {}
  ~ObKVCache() = default;

  int init(int64_t capacity)
  {
    capacity_ = capacity;
    size_     = 0;
    return 0;
  }

  /** Get value by key. Returns true if found, false otherwise. */
  bool get(const Key &key, Value &value)
  {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = map_.find(key);
    if (it == map_.end()) {
      return false;
    }
    // Move to front (LRU)
    lru_list_.erase(it->second.lru_iter_);
    lru_list_.push_front(key);
    it->second.lru_iter_ = lru_list_.begin();
    value = it->second.value_;
    return true;
  }

  /** Put a key-value pair into the cache. */
  int put(const Key &key, const Value &value)
  {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      // Update existing
      it->second.value_ = value;
      lru_list_.erase(it->second.lru_iter_);
      lru_list_.push_front(key);
      it->second.lru_iter_ = lru_list_.begin();
      return 0;
    }
    // Evict if needed
    while (size_ >= capacity_ && !lru_list_.empty()) {
      Key oldest = lru_list_.back();
      lru_list_.pop_back();
      map_.erase(oldest);
      size_--;
    }
    // Insert new
    lru_list_.push_front(key);
    CacheEntry entry{value, lru_list_.begin()};
    map_[key] = entry;
    size_++;
    return 0;
  }

  /** Erase a key. */
  int erase(const Key &key)
  {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      lru_list_.erase(it->second.lru_iter_);
      map_.erase(it);
      size_--;
    }
    return 0;
  }

  int64_t size() const { return size_; }
  void clear()
  {
    std::lock_guard<std::mutex> guard(mutex_);
    map_.clear();
    lru_list_.clear();
    size_ = 0;
  }

private:
  struct CacheEntry
  {
    Value value_;
    typename std::list<Key>::iterator lru_iter_;
  };

  int64_t                                       capacity_;
  int64_t                                       size_;
  std::unordered_map<Key, CacheEntry>            map_;
  std::list<Key>                                 lru_list_;
  mutable std::mutex                             mutex_;
};

}  // namespace blocksstable
}  // namespace oceanbase
