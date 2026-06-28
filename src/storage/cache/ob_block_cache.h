/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/blockblocksstable/ob_micro_block_cache.h */

#pragma once

#include <string>

#include "storage/cache/ob_kv_cache.h"

namespace oceanbase {
namespace blocksstable {

/**
 * ObMicroBlockCacheKey — identifies a micro block.
 */
struct ObMicroBlockCacheKey
{
  int64_t macro_block_id_;
  int64_t block_offset_;
  int64_t block_size_;

  ObMicroBlockCacheKey() : macro_block_id_(0), block_offset_(0), block_size_(0) {}
  ObMicroBlockCacheKey(int64_t mb_id, int64_t offset, int64_t size)
    : macro_block_id_(mb_id), block_offset_(offset), block_size_(size) {}

  bool operator==(const ObMicroBlockCacheKey &o) const
  {
    return macro_block_id_ == o.macro_block_id_ &&
           block_offset_ == o.block_offset_ &&
           block_size_ == o.block_size_;
  }
};

}  // namespace blocksstable
}  // namespace oceanbase

// Hash specialization for ObMicroBlockCacheKey
namespace std {
template <>
struct hash<oceanbase::blocksstable::ObMicroBlockCacheKey>
{
  size_t operator()(const oceanbase::blocksstable::ObMicroBlockCacheKey &k) const
  {
    return hash<int64_t>()(k.macro_block_id_) ^
           hash<int64_t>()(k.block_offset_) ^
           hash<int64_t>()(k.block_size_);
  }
};
}  // namespace std

namespace oceanbase {
namespace blocksstable {

/**
 * ObMicroBlockCacheValue — cached decompressed micro block data.
 */
struct ObMicroBlockCacheValue
{
  std::string data_;
  int64_t     access_count_;

  ObMicroBlockCacheValue() : access_count_(0) {}
};

/**
 * ObDataMicroBlockCache — cache for decompressed micro blocks.
 * Simplified from OB 4.4.2 ObDataMicroBlockCache.
 */
using ObDataMicroBlockCache = ObKVCache<ObMicroBlockCacheKey, ObMicroBlockCacheValue>;

}  // namespace blocksstable
}  // namespace oceanbase
