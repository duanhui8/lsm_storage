/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/memtable/ob_memtable_data.h */

#pragma once

#include <cstring>

#include "storage/ob_define.h"
#include "storage/ob_i_store.h"

namespace oceanbase {
namespace memtable {

/**
 * ObMemtableDataHeader — the structure actually written into ObMvccTransNode::buf_.
 * Identical to OB 4.4.2 in structure.
 * From: /opt/oceanbase4.4.2/src/storage/memtable/ob_memtable_data.h:26-68
 */
class ObMemtableDataHeader
{
public:
  ObMemtableDataHeader(blocksstable::ObDmlFlag dml_flag, int64_t buf_len)
      : dml_flag_(dml_flag), buf_len_(buf_len) {}
  ~ObMemtableDataHeader() {}

  inline int64_t len() const { return buf_len_; }

  static int build(ObMemtableDataHeader *new_data,
                   blocksstable::ObDmlFlag dml_flag,
                   int64_t buf_len,
                   const char *buf)
  {
    if (buf_len < 0) {
      return -1;  // OB_NOT_INIT
    }
    if (nullptr == buf || 0 == buf_len) {
      // empty data, ok
    } else if (OB_ISNULL(new (new_data) ObMemtableDataHeader(dml_flag, buf_len))) {
      return -1;  // OB_ALLOCATE_MEMORY_FAILED
    } else {
      MEMCPY(new_data->buf_, buf, buf_len);
    }
    return 0;  // OB_SUCCESS
  }

  void set_dml_flag(blocksstable::ObDmlFlag flag) { dml_flag_ = flag; }
  void set_buf_len(int64_t len) { buf_len_ = len; }

  blocksstable::ObDmlFlag dml_flag_;
  int64_t                 buf_len_;
  char                    buf_[0];  // flexible array member
};

/**
 * ObMemtableData — lightweight pass-by-value structure for parameter passing.
 * Identical to OB 4.4.2.
 * From: /opt/oceanbase4.4.2/src/storage/memtable/ob_memtable_data.h:70-101
 */
class ObMemtableData
{
public:
  ObMemtableData()
      : dml_flag_(blocksstable::ObDmlFlag::DF_MAX), buf_len_(0), buf_(nullptr) {}
  ObMemtableData(blocksstable::ObDmlFlag dml_flag, int64_t buf_len, const char *buf)
      : dml_flag_(dml_flag), buf_len_(buf_len), buf_(buf) {}
  ~ObMemtableData() {}

  void reset()
  {
    dml_flag_ = blocksstable::ObDmlFlag::DF_MAX;
    buf_len_  = 0;
    buf_      = nullptr;
  }

  void set(blocksstable::ObDmlFlag dml_flag, const int64_t data_len, const char *buf)
  {
    dml_flag_ = dml_flag;
    buf_len_  = data_len;
    buf_      = buf;
  }

  inline int64_t size() const { return sizeof(ObMemtableDataHeader) + buf_len_; }

  blocksstable::ObDmlFlag dml_flag_;
  int64_t                 buf_len_;
  const char             *buf_;
};

}  // namespace memtable
}  // namespace oceanbase
