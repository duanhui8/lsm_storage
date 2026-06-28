/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/memtable/mvcc/ob_mvcc_row.h */

#pragma once

#include <atomic>
#include <mutex>

#include "storage/ob_define.h"
#include "storage/ob_i_store.h"
#include "storage/memtable/ob_memtable_data.h"

namespace oceanbase {
namespace memtable {

using storage::OB_INVALID_TRANS_ID;
using storage::OB_INVALID_VERSION;
using blocksstable::ObDmlFlag;

/**
 * TransNodeFlag — atomic flag set for ObMvccTransNode.
 * Simplified from OB 4.4.2: only F_COMMITTED, F_ABORTED, F_INIT.
 * From: ob_mvcc_row.h TransNodeFlag
 */
struct TransNodeFlag
{
  static const uint8_t F_INIT      = 0x01;
  static const uint8_t F_COMMITTED = 0x02;
  static const uint8_t F_ABORTED   = 0x04;
};

/**
 * ObMvccTransNode — a single version node in the doubly-linked version chain.
 * Simplified from OB 4.4.2 ob_mvcc_row.h:71-275:
 *   Removed: scn_, write_epoch_, seq_no_, tx_end_scn_, modify_count_,
 *            acc_checksum_, version_, snapshot_version_barrier_,
 *            F_ELR, F_DELAYED_CLEANOUT, F_INCOMPLETE_STATE
 *   Kept:    tx_id_, trans_version_, prev_, next_, type_, flag_, buf_[0]
 */
struct ObMvccTransNode
{
  storage::ObTransID tx_id_;            // which transaction wrote this
  int64_t            trans_version_;    // commit version (set on commit)
  ObMvccTransNode   *prev_;             // older version
  ObMvccTransNode   *next_;             // newer version
  uint8_t            type_;             // NDT_NORMAL or NDT_COMPACT (simplified)
  uint8_t            flag_;             // TransNodeFlag bits (atomic)
  char               buf_[0];           // flexible array: ObMemtableDataHeader + row data

  // ---- flag helpers (from OB 4.4.2) ----
  bool is_committed() const { return flag_ & TransNodeFlag::F_COMMITTED; }
  bool is_aborted() const   { return flag_ & TransNodeFlag::F_ABORTED; }

  void trans_commit(const int64_t commit_version)
  {
    trans_version_ = commit_version;
    flag_ |= TransNodeFlag::F_COMMITTED;
    flag_ &= ~TransNodeFlag::F_ABORTED;
  }

  void trans_abort()
  {
    flag_ |= TransNodeFlag::F_ABORTED;
    flag_ &= ~TransNodeFlag::F_COMMITTED;
  }

  /** Get the DML flag from the ObMemtableDataHeader stored in buf_. */
  blocksstable::ObDmlFlag get_dml_flag() const
  {
    const ObMemtableDataHeader *hdr = reinterpret_cast<const ObMemtableDataHeader *>(buf_);
    return hdr->dml_flag_;
  }

  int64_t get_data_size() const
  {
    const ObMemtableDataHeader *hdr = reinterpret_cast<const ObMemtableDataHeader *>(buf_);
    return sizeof(ObMemtableDataHeader) + hdr->buf_len_;
  }
};

/**
 * ObMvccRow — per-key MVCC state, holds the version chain.
 * Simplified from OB 4.4.2 ob_mvcc_row.h:277-520:
 *   Removed: first_dml_flag_, last_dml_flag_, update_since_compact_,
 *            total_trans_node_cnt_, latest_compact_ts_, last_compact_cnt_,
 *            max_elr_trans_version_, max_modify_scn_, min_modify_scn_,
 *            max_trans_id_, max_elr_trans_id_, latest_compact_node_, index_
 *   Kept:    latch_, flag_, max_trans_version_, list_head_
 */
struct ObMvccRow
{
  static const uint8_t F_HASH_INDEX  = 0x01;  // present in hash table
  static const uint8_t F_BTREE_INDEX = 0x02;  // present in btree

  std::mutex         latch_;
  uint8_t            flag_;               // F_HASH_INDEX, F_BTREE_INDEX
  int64_t            max_trans_version_;  // highest committed version in chain
  ObMvccTransNode   *list_head_;          // newest -> oldest doubly linked list

  ObMvccRow()
      : flag_(0), max_trans_version_(0), list_head_(nullptr) {}

  ~ObMvccRow() = default;

  bool is_empty() const { return nullptr == list_head_; }

  bool is_hash_indexed() const { return flag_ & F_HASH_INDEX; }
  void set_hash_indexed() { flag_ |= F_HASH_INDEX; }

  bool is_btree_indexed() const { return flag_ & F_BTREE_INDEX; }
  void set_btree_indexed() { flag_ |= F_BTREE_INDEX; }

  ObMvccTransNode *get_list_head() const { return list_head_; }

  /**
   * Check Transaction Set violation.
   * From OB 4.4.2 ObMvccRow::is_transaction_set_violation()
   */
  bool is_transaction_set_violation(int64_t snapshot_version) const
  {
    if (snapshot_version == OB_INVALID_VERSION) {
      return false;
    }
    return max_trans_version_ > snapshot_version;
  }

  /**
   * mvcc_write — insert a new trans node at the head of the version chain.
   * Simplified from OB 4.4.2 ObMvccRow::mvcc_write().
   *
   * @param ctx           Store context with tx_id and snapshot
   * @param node          The new trans node to insert
   * @param check_exist   Whether to check for duplicate primary key
   * @param write_conflict Output: true if write-write conflict detected
   * @return 0 on success, -1 on conflict/error
   */
  int mvcc_write(const storage::ObStoreCtx &ctx,
                 ObMvccTransNode           &node,
                 bool                       check_exist,
                 bool                      &write_conflict);
};

}  // namespace memtable
}  // namespace oceanbase
