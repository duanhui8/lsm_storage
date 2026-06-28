/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/ob_i_store.h */

#pragma once

#include <cstring>
#include <string>
#include <vector>

#include "storage/ob_define.h"

namespace oceanbase {
namespace storage {

using ObDmlFlag = memtable::ObDmlFlag;

/**
 * ObStoreRowkey — simplified row key representation.
 * In OB 4.4.2, this is ObStoreRowkey wrapping an array of ObObj.
 * For MiniOB, we use a simple byte buffer as the row key.
 */
class ObStoreRowkey
{
public:
  ObStoreRowkey() : length_(0) {}
  ObStoreRowkey(const char *data, int64_t length) : length_(length)
  {
    if (length > 0 && data != nullptr) {
      buf_.assign(data, data + length);
    }
  }

  explicit ObStoreRowkey(const std::vector<uint8_t> &key_data)
  {
    buf_.assign(reinterpret_cast<const char *>(key_data.data()),
                reinterpret_cast<const char *>(key_data.data() + key_data.size()));
    length_ = key_data.size();
  }

  void reset()
  {
    buf_.clear();
    length_ = 0;
  }

  const char *get_data() const { return length_ > 0 ? buf_.data() : nullptr; }
  int64_t     get_length() const { return length_; }

  bool is_valid() const { return length_ > 0 && !buf_.empty(); }

  int compare(const ObStoreRowkey &other) const
  {
    if (length_ < other.length_) return -1;
    if (length_ > other.length_) return 1;
    if (length_ == 0) return 0;
    return memcmp(buf_.data(), other.buf_.data(), length_);
  }

  bool equal(const ObStoreRowkey &other) const
  {
    if (length_ != other.length_) return false;
    if (length_ == 0) return true;
    return memcmp(buf_.data(), other.buf_.data(), length_) == 0;
  }

  uint64_t murmurhash(uint64_t seed) const;

private:
  std::string buf_;       // owned buffer
  int64_t     length_;
};

/**
 * ObStoreRow — represents a stored row with multi-version info.
 * Simplified from OB 4.4.2: removed ObNewRow, column_ids, scan_index, etc.
 */
struct ObStoreRow
{
  ObDmlFlag          dml_flag_;         // INSERT/UPDATE/DELETE
  int64_t            trans_version_;    // commit version (snapshot)
  ObTransID          trans_id_;         // transaction that wrote this
  ObStoreRowkey      rowkey_;           // row key
  std::vector<char>  row_value_;        // serialized row data (all columns)
  bool               is_deleted_;       // tombstone marker

  ObStoreRow() : dml_flag_(ObDmlFlag::DF_MAX), trans_version_(0),
                 trans_id_(OB_INVALID_TRANS_ID), is_deleted_(false) {}

  void reset()
  {
    dml_flag_       = ObDmlFlag::DF_MAX;
    trans_version_  = 0;
    trans_id_       = OB_INVALID_TRANS_ID;
    rowkey_.reset();
    row_value_.clear();
    is_deleted_     = false;
  }
};

/**
 * ObStoreCtx — transaction/read context.
 * Simplified from OB 4.4.2: removed ls_id, ls, tablet_id, etc.
 */
struct ObStoreCtx
{
  ObTransID tx_id_;              // my transaction id
  int64_t   snapshot_version_;   // read snapshot version
  int64_t   timeout_;            // operation timeout (ms)

  ObStoreCtx()
      : tx_id_(OB_INVALID_TRANS_ID),
        snapshot_version_(OB_INVALID_VERSION),
        timeout_(0) {}

  void reset()
  {
    tx_id_             = OB_INVALID_TRANS_ID;
    snapshot_version_  = 0;
    timeout_           = 0;
  }

  bool is_valid() const { return tx_id_ >= 0; }
};

}  // namespace storage
}  // namespace oceanbase
