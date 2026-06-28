/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/memtable/mvcc/ob_mvcc_row.h ObMvccRow::mvcc_write */

#include "storage/memtable/mvcc/ob_mvcc_row.h"
#include "storage/memtable/ob_memtable_data.h"

namespace oceanbase {
namespace memtable {

using storage::OB_INVALID_VERSION;
using storage::OB_INVALID_TRANS_ID;

/**
 * mvcc_write — insert a new trans node at the head of the version chain.
 * Simplified from OB 4.4.2 ObMvccRow::mvcc_write().
 *
 * Walks the version chain from list_head_ (newest → oldest).
 * Handles the following cases:
 *   - head is nullptr → insert as head
 *   - head is COMMITTED → insert as new head (above committed)
 *   - head is ABORTED → skip (unlink), re-examine next
 *   - head has same tx_id → append (multi-statement txn)
 *   - head belongs to different TX and is not decided → WRITE-WRITE CONFLICT
 */
int ObMvccRow::mvcc_write(const storage::ObStoreCtx &ctx,
                           ObMvccTransNode           &node,
                           bool                       check_exist,
                           bool                      &write_conflict)
{
  write_conflict = false;
  std::lock_guard<std::mutex> guard(latch_);

  // Walk version chain from head (newest)
  ObMvccTransNode *head = list_head_;
  while (head != nullptr) {
    if (head->is_aborted()) {
      // Skip aborted nodes — they will be unlinked during cleanup
      head = head->prev_;
      continue;
    }

    if (head->is_committed()) {
      // Committed version is above us — insert after it
      break;
    }

    // Head is not committed and not aborted — it's an in-flight transaction
    if (head->tx_id_ == ctx.tx_id_) {
      // Same transaction — can append (multi-statement txn)
      break;
    } else {
      // Different transaction in-flight — write-write conflict
      write_conflict = true;
      return -1;  // OB_TRY_LOCK_ROW_CONFLICT
    }
  }

  // Check TSC (Transaction Set violation) — a newer committed version exists
  // that is not visible to my snapshot
  if (is_transaction_set_violation(ctx.snapshot_version_)) {
    return -1;  // OB_TRANSACTION_SET_VIOLATION
  }

  // Link the new node at the head of the chain
  node.next_ = nullptr;
  node.prev_ = head;

  if (head != nullptr) {
    // head is the previous newest — link it to our node
    ObMvccTransNode *old_next_of_head = head->next_;
    node.prev_ = head;
    if (old_next_of_head != nullptr) {
      old_next_of_head->prev_ = &node;
      node.next_ = old_next_of_head;
    }
    head->next_ = &node;
    node.prev_ = head;
  }

  // Our node becomes the new head
  list_head_ = &node;

  // Update max_trans_version_ (optimistic — set to max possible)
  if (ctx.snapshot_version_ > max_trans_version_ &&
      ctx.snapshot_version_ != OB_INVALID_VERSION) {
    max_trans_version_ = ctx.snapshot_version_;
  }

  return 0;  // OB_SUCCESS
}

}  // namespace memtable
}  // namespace oceanbase
