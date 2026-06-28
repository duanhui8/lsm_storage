/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/memtable/ob_memtable_key.h */

#pragma once

#include <cstring>

#include "storage/ob_i_store.h"

namespace oceanbase {
namespace memtable {

/**
 * ObMemtableKey — wraps a rowkey pointer with cached hash value.
 * Simplified from OB 4.4.2: removed ObStoreRowkey dependency, using byte key directly.
 * From: /opt/oceanbase4.4.2/src/storage/memtable/ob_memtable_key.h:28-293
 */
class ObMemtableKey
{
public:
  ObMemtableKey() : key_data_(nullptr), key_len_(0), hash_val_(0), rowkey_(nullptr) {}

  ObMemtableKey(const char *data, int64_t len)
      : key_data_(data), key_len_(len), hash_val_(0), rowkey_(nullptr)
  {
    if (len > 0 && data != nullptr) {
      hash_val_ = murmurhash_(0);
    }
  }

  ~ObMemtableKey() = default;

  /** Encode from a rowkey. */
  int encode(const storage::ObStoreRowkey *rowkey)
  {
    if (OB_ISNULL(rowkey) || !rowkey->is_valid()) {
      return -1;
    }
    key_data_ = rowkey->get_data();
    key_len_  = rowkey->get_length();
    rowkey_   = rowkey;
    hash_val_ = murmurhash_(0);
    return 0;
  }

  /** Deep copy into an allocator-provided buffer.
   *  Simplified: stores the key bytes inline in a heap-allocated copy.
   */
  int dup(ObMemtableKey *&new_key) const;

  /** Compare two keys. */
  int compare(const ObMemtableKey &other) const
  {
    if (key_len_ < other.key_len_) return -1;
    if (key_len_ > other.key_len_) return 1;
    if (key_len_ == 0) return 0;
    return std::memcmp(key_data_, other.key_data_, key_len_);
  }

  /** Equality check. */
  bool equal(const ObMemtableKey &other) const
  {
    if (hash_val_ != 0 && other.hash_val_ != 0 && hash_val_ != other.hash_val_) {
      return false;
    }
    if (key_len_ != other.key_len_) return false;
    if (key_len_ == 0) return true;
    return std::memcmp(key_data_, other.key_data_, key_len_) == 0;
  }

  uint64_t hash() const { return hash_val_; }

  const char *get_key_data() const { return key_data_; }
  int64_t     get_key_len() const { return key_len_; }
  const storage::ObStoreRowkey *get_rowkey() const { return rowkey_; }

  /** Calculate murmurhash of the key. */
  uint64_t murmurhash_(uint64_t seed) const;

private:
  const char                     *key_data_;   // NOT owned — points to external memory
  int64_t                         key_len_;
  mutable uint64_t                hash_val_;
  const storage::ObStoreRowkey   *rowkey_;     // NOT owned — set by encode()
};

/**
 * ObStoreRowkeyWrapper — lightweight wrapper used as btree key type.
 * From: /opt/oceanbase4.4.2/src/storage/memtable/ob_memtable_key.h:294-314
 */
class ObStoreRowkeyWrapper
{
public:
  ObStoreRowkeyWrapper() : rowkey_(nullptr) {}
  explicit ObStoreRowkeyWrapper(const storage::ObStoreRowkey *rowkey) : rowkey_(rowkey) {}
  ~ObStoreRowkeyWrapper() = default;

  const storage::ObStoreRowkey *get_rowkey() const { return rowkey_; }
  void set_rowkey(const storage::ObStoreRowkey *rowkey) { rowkey_ = rowkey; }

  int compare(const ObStoreRowkeyWrapper &other) const
  {
    if (OB_ISNULL(rowkey_) || OB_ISNULL(other.rowkey_)) {
      return -1;
    }
    return rowkey_->compare(*other.rowkey_);
  }

  bool equal(const ObStoreRowkeyWrapper &other) const
  {
    if (OB_ISNULL(rowkey_) || OB_ISNULL(other.rowkey_)) {
      return false;
    }
    return rowkey_->equal(*other.rowkey_);
  }

  uint64_t hash() const
  {
    if (OB_ISNULL(rowkey_)) return 0;
    return rowkey_->murmurhash(0);
  }

private:
  const storage::ObStoreRowkey *rowkey_;
};

/**
 * ObMemtableKeyGenerator — generates ObMemtableKey from ObStoreRow.
 * Simplified from OB 4.4.2: ob_memtable_key.h:315+
 */
class ObMemtableKeyGenerator
{
public:
  static int generate_memtable_key(const storage::ObStoreRow &row, ObMemtableKey &key)
  {
    if (!row.rowkey_.is_valid()) {
      return -1;
    }
    return key.encode(&row.rowkey_);
  }
};

}  // namespace memtable
}  // namespace oceanbase
