/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/memtable/mvcc/ob_mvcc_define.h */

#pragma once

#include "storage/ob_define.h"
#include "storage/ob_i_store.h"
#include "storage/memtable/ob_memtable_data.h"
#include "storage/memtable/ob_memtable_key.h"
#include "storage/memtable/mvcc/ob_mvcc_row.h"

namespace oceanbase {
namespace memtable {

using storage::OB_INVALID_TRANS_ID;
using storage::OB_INVALID_VERSION;

/**
 * ObTxNodeArg — argument for mvcc_write.
 * Simplified from OB 4.4.2 ob_mvcc_define.h:30-173:
 *   Removed: scn_, seq_no_, write_epoch_, modify_count_, acc_checksum_, memstore_version_
 *   Kept:    tx_id_, data_, column_cnt_
 */
struct ObTxNodeArg
{
  storage::ObTransID  tx_id_;
  const ObMemtableData *data_;
  int64_t              column_cnt_;

  ObTxNodeArg()
      : tx_id_(OB_INVALID_TRANS_ID), data_(nullptr), column_cnt_(0) {}

  ObTxNodeArg(storage::ObTransID tx_id,
              const ObMemtableData *data,
              int64_t column_cnt)
      : tx_id_(tx_id), data_(data), column_cnt_(column_cnt) {}
};

/**
 * ObMvccWriteResult — result of mvcc write operation.
 * Simplified from OB 4.4.2 ob_mvcc_define.h:174-301:
 *   Removed: is_new_locked_, lock_state_, tx_callback_, is_checked_
 *   Kept:    can_insert_, need_insert_, is_mvcc_undo_, tx_node_, value_, mtk_
 */
struct ObMvccWriteResult
{
  bool              can_insert_;     // whether the insert can proceed
  bool              need_insert_;    // whether we need to insert
  bool              is_mvcc_undo_;   // whether this was an undo operation
  ObMvccTransNode  *tx_node_;        // the newly created trans node
  ObMvccRow        *value_;          // the mvcc row (anchor)
  ObMemtableKey     mtk_;            // the stored key

  ObMvccWriteResult()
      : can_insert_(false),
        need_insert_(false),
        is_mvcc_undo_(false),
        tx_node_(nullptr),
        value_(nullptr) {}

  bool has_insert() const
  {
    return can_insert_ && need_insert_ && !is_mvcc_undo_;
  }

  void reset()
  {
    can_insert_   = false;
    need_insert_  = false;
    is_mvcc_undo_ = false;
    tx_node_      = nullptr;
    value_        = nullptr;
  }
};

}  // namespace memtable
}  // namespace oceanbase
