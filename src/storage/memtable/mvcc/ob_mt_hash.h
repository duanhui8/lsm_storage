/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/memtable/ob_mt_hash.h */

#pragma once

#include <unordered_map>
#include <mutex>

#include "storage/memtable/ob_memtable_key.h"
#include "storage/memtable/mvcc/ob_mvcc_row.h"

namespace oceanbase {
namespace memtable {

/**
 * ObMtHash — hash table optimized for point select.
 * Simplified from OB 4.4.2: using std::unordered_map instead of lock-free open-addressing.
 * Key: ObStoreRowkeyWrapper (wraps ObStoreRowkey*)
 * Value: ObMvccRow*
 *
 * From: /opt/oceanbase4.4.2/src/storage/memtable/ob_mt_hash.h
 */
class ObMtHash
{
public:
  ObMtHash()  = default;
  ~ObMtHash() = default;

  /**
   * Insert or get a key-value pair.
   * @return 0 on success, -1 if key exists (but value is returned)
   */
  int set(const ObStoreRowkeyWrapper &key, ObMvccRow *value)
  {
    auto iter = map_.find(key.hash());
    if (iter != map_.end()) {
      // Key already exists — return the existing value
      return -1;  // OB_ENTRY_EXIST
    }
    map_[key.hash()] = value;
    return 0;  // OB_SUCCESS
  }

  /**
   * Get the value for a key.
   * @return 0 on success (value set), -1 if not found
   */
  int get(const ObStoreRowkeyWrapper &key, ObMvccRow *&value)
  {
    auto iter = map_.find(key.hash());
    if (iter == map_.end()) {
      value = nullptr;
      return -1;  // OB_ENTRY_NOT_EXIST
    }
    value = iter->second;
    return 0;  // OB_SUCCESS
  }

  /**
   * Get or insert — if key exists, return existing. Otherwise create via creator.
   */
  int get_or_create(const ObStoreRowkeyWrapper &key, ObMvccRow *&value,
                    std::function<int(ObMvccRow *&)> creator)
  {
    auto iter = map_.find(key.hash());
    if (iter != map_.end()) {
      value = iter->second;
      return 0;
    }
    int ret = creator(value);
    if (ret == 0 && value != nullptr) {
      map_[key.hash()] = value;
    }
    return ret;
  }

  /** Erase a key. */
  int erase(const ObStoreRowkeyWrapper &key)
  {
    map_.erase(key.hash());
    return 0;
  }

  /** Clear all entries. */
  void clear() { map_.clear(); }

  size_t size() const { return map_.size(); }

private:
  std::unordered_map<uint64_t, ObMvccRow *> map_;
};

}  // namespace memtable
}  // namespace oceanbase
