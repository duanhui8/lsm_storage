/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/memtable/mvcc/ob_mvcc_engine.h methods */

#include <cstring>

#include "storage/memtable/mvcc/ob_mvcc_engine.h"

namespace oceanbase {
namespace memtable {

using storage::OB_INVALID_VERSION;
using storage::OB_INVALID_TRANS_ID;
using blocksstable::ObDmlFlag;

ObMvccEngine::ObMvccEngine()
    : is_inited_(false), query_engine_(nullptr), memtable_(nullptr) {}

ObMvccEngine::~ObMvccEngine() { destroy(); }

int ObMvccEngine::init(ObQueryEngine *query_engine, ObMemtable *memtable)
{
  if (OB_ISNULL(query_engine) || OB_ISNULL(memtable)) {
    return -1;
  }
  query_engine_ = query_engine;
  memtable_     = memtable;
  is_inited_    = true;
  return 0;
}

void ObMvccEngine::destroy()
{
  is_inited_     = false;
  query_engine_  = nullptr;
  memtable_       = nullptr;
}

// ========== create_kv (from OB 4.4.2 ob_mvcc_engine.cpp) ==========
int ObMvccEngine::create_kv(const ObMemtableKey *key,
                             ObMemtableKey        *stored_key,
                             ObMvccRow            *&value)
{
  return create_kv_(key, stored_key, value);
}

int ObMvccEngine::create_kv_(const ObMemtableKey *key,
                              ObMemtableKey       *stored_key,
                              ObMvccRow           *&value)
{
  int ret = query_engine_get_(key, value, stored_key);
  if (ret != 0 || value == nullptr) {
    // Key does not exist — create new ObMvccRow
    value = new ObMvccRow();
    if (OB_ISNULL(value)) {
      return -1;  // OB_ALLOCATE_MEMORY_FAILED
    }
    // Insert into query engine
    ret = query_engine_->set(key, value);
    if (ret == -1) {
      // Raced with another writer — use existing
      delete value;
      return query_engine_get_(key, value, stored_key);
    }
  }
  return 0;
}

int ObMvccEngine::ensure_kv(const ObMemtableKey *stored_key, ObMvccRow *value)
{
  return query_engine_->ensure(stored_key, value);
}

int ObMvccEngine::query_engine_get_(const ObMemtableKey *parameter_key,
                                     ObMvccRow           *&row,
                                     ObMemtableKey        *returned_key)
{
  return query_engine_->get(parameter_key, row, returned_key);
}

// ========== mvcc_write (from OB 4.4.2 ob_mvcc_engine.cpp) ==========
int ObMvccEngine::mvcc_write(storage::ObStoreCtx &ctx,
                              ObMvccRow           &value,
                              const ObTxNodeArg   &arg,
                              const bool           check_exist,
                              ObMvccWriteResult   &res)
{
  res.reset();

  // Build the trans node from arg
  ObMvccTransNode *node = nullptr;
  int ret = build_tx_node_(arg, node);
  if (ret != 0 || OB_ISNULL(node)) {
    return -1;
  }

  // Write into the version chain
  bool write_conflict = false;
  ret = value.mvcc_write(ctx, *node, check_exist, write_conflict);

  if (ret != 0) {
    // Free the node on failure
    delete[] reinterpret_cast<char *>(node);
    if (write_conflict) {
      return -1;  // OB_TRY_LOCK_ROW_CONFLICT
    }
    return ret;
  }

  // Fill result
  res.can_insert_  = true;
  res.need_insert_ = true;
  res.tx_node_     = node;
  res.value_       = &value;
  return 0;
}

int ObMvccEngine::build_tx_node_(const ObTxNodeArg &arg, ObMvccTransNode *&node)
{
  if (OB_ISNULL(arg.data_)) {
    node = nullptr;
    return -1;
  }

  int64_t data_size = arg.data_->size();
  int64_t total_size = sizeof(ObMvccTransNode) + data_size;

  char *buf = new char[total_size];
  if (OB_ISNULL(buf)) {
    return -1;  // OB_ALLOCATE_MEMORY_FAILED
  }

  node = new (buf) ObMvccTransNode();
  node->tx_id_         = arg.tx_id_;
  node->trans_version_ = OB_INVALID_VERSION;
  node->prev_          = nullptr;
  node->next_          = nullptr;
  node->type_          = 0;
  node->flag_          = TransNodeFlag::F_INIT;

  // Build ObMemtableDataHeader into buf_[0] via placement new + MEMCPY
  // (same pattern as OB 4.4.2 ObMTKVBuilder::build_trans_node)
  ObMemtableDataHeader *hdr = reinterpret_cast<ObMemtableDataHeader *>(node->buf_);
  new (hdr) ObMemtableDataHeader(arg.data_->dml_flag_, arg.data_->buf_len_);
  if (arg.data_->buf_len_ > 0 && arg.data_->buf_ != nullptr) {
    MEMCPY(hdr->buf_, arg.data_->buf_, arg.data_->buf_len_);
  }

  // Auto-commit for MiniOB's simplified transaction model
  // In full OB 4.4.2, commit happens separately via ObPartTransCtx
  node->trans_commit(1 /* commit version */);

  return 0;
}

void ObMvccEngine::mvcc_undo(ObMvccRow *value)
{
  if (OB_ISNULL(value)) return;
  // Remove the head node (our write)
  ObMvccTransNode *head = value->get_list_head();
  if (head != nullptr) {
    head->trans_abort();
    // Physically unlink from head
    value->list_head_ = head->prev_;
    if (head->prev_ != nullptr) {
      head->prev_->next_ = nullptr;
    }
    // Free memory
    delete[] reinterpret_cast<char *>(head);
  }
}

void ObMvccEngine::finish_kv(ObMvccWriteResult &res)
{
  // In OB 4.4.2, this makes the tx node visible for reads.
  // For MiniOB's simplified model, the node is visible immediately.
  // Just mark can_insert_ = false so callers know it's done.
  res.can_insert_ = false;
}

// ========== Read path (simplified from OB 4.4.2) ==========

int ObMvccEngine::get(const storage::ObStoreCtx &ctx,
                       const ObMemtableKey         *parameter_key,
                       std::vector<storage::ObStoreRow> &results)
{
  ObMvccRow *row = nullptr;
  ObMemtableKey internal_key;
  int ret = query_engine_get_(parameter_key, row, &internal_key);
  if (ret != 0 || OB_ISNULL(row)) {
    return -1;  // OB_ENTRY_NOT_EXIST
  }

  // Walk the version chain from head (newest) to find first visible version
  ObMvccTransNode *node = row->get_list_head();
  while (node != nullptr) {
    if (node->is_committed()) {
      // Check visible to snapshot
      if (ctx.snapshot_version_ == OB_INVALID_VERSION ||
          node->trans_version_ <= ctx.snapshot_version_) {
        storage::ObStoreRow srow;
        const ObMemtableDataHeader *hdr = reinterpret_cast<const ObMemtableDataHeader *>(node->buf_);
        srow.dml_flag_      = hdr->dml_flag_;
        srow.trans_version_ = node->trans_version_;
        srow.trans_id_      = node->tx_id_;
        srow.row_value_.assign(hdr->buf_, hdr->buf_ + hdr->buf_len_);
        srow.is_deleted_    = (hdr->dml_flag_ == blocksstable::ObDmlFlag::DF_DELETE);
        results.push_back(srow);
        return 0;
      }
    } else if (node->is_aborted()) {
      // Skip aborted
    } else if (node->tx_id_ == ctx.tx_id_) {
      // Read our own uncommitted write
      storage::ObStoreRow srow;
      const ObMemtableDataHeader *hdr = reinterpret_cast<const ObMemtableDataHeader *>(node->buf_);
      srow.dml_flag_      = hdr->dml_flag_;
      srow.trans_version_ = node->trans_version_;
      srow.trans_id_      = node->tx_id_;
      srow.row_value_.assign(hdr->buf_, hdr->buf_ + hdr->buf_len_);
      srow.is_deleted_    = (hdr->dml_flag_ == blocksstable::ObDmlFlag::DF_DELETE);
      results.push_back(srow);
      return 0;
    }
    node = node->prev_;
  }

  return -1;  // OB_ENTRY_NOT_EXIST
}

int ObMvccEngine::scan(const storage::ObStoreCtx &ctx,
                        const ObMemtableKey        *start_key,
                        bool                        start_exclude,
                        const ObMemtableKey        *end_key,
                        bool                        end_exclude,
                        std::vector<storage::ObStoreRow> &results)
{
  // Use QueryEngine's range scan
  ObIQueryEngineIterator *iter = nullptr;
  int ret = query_engine_->scan(start_key, start_exclude, end_key, end_exclude, iter);
  if (ret != 0) {
    return ret;
  }

  while (iter->next() == 0) {
    ObMvccRow *row = iter->get_value();
    if (row == nullptr || row->is_empty()) continue;

    // Walk version chain to find first visible version
    ObMvccTransNode *node = row->get_list_head();
    while (node != nullptr) {
      if (node->is_committed() &&
          (ctx.snapshot_version_ == OB_INVALID_VERSION ||
           node->trans_version_ <= ctx.snapshot_version_)) {
        storage::ObStoreRow srow;
        const ObMemtableDataHeader *hdr = reinterpret_cast<const ObMemtableDataHeader *>(node->buf_);
        srow.dml_flag_      = hdr->dml_flag_;
        srow.trans_version_ = node->trans_version_;
        srow.trans_id_      = node->tx_id_;
        srow.row_value_.assign(hdr->buf_, hdr->buf_ + hdr->buf_len_);
        srow.is_deleted_    = (hdr->dml_flag_ == blocksstable::ObDmlFlag::DF_DELETE);
        results.push_back(srow);
        break;
      } else if (node->is_aborted()) {
        // skip
      } else if (node->tx_id_ == ctx.tx_id_) {
        // Read own uncommitted write
        storage::ObStoreRow srow;
        const ObMemtableDataHeader *hdr = reinterpret_cast<const ObMemtableDataHeader *>(node->buf_);
        srow.dml_flag_      = hdr->dml_flag_;
        srow.trans_version_ = node->trans_version_;
        srow.trans_id_      = node->tx_id_;
        srow.row_value_.assign(hdr->buf_, hdr->buf_ + hdr->buf_len_);
        srow.is_deleted_    = (hdr->dml_flag_ == blocksstable::ObDmlFlag::DF_DELETE);
        results.push_back(srow);
        break;
      }
      node = node->prev_;
    }
  }

  query_engine_->revert_iter(iter);
  return 0;
}

int ObMvccEngine::check_row_locked(const storage::ObStoreCtx &ctx,
                                    const ObMemtableKey        *key,
                                    bool                       &is_locked,
                                    storage::ObTransID         &lock_trans_id)
{
  ObMvccRow *row = nullptr;
  ObMemtableKey internal_key;
  int ret = query_engine_get_(key, row, &internal_key);
  if (ret != 0 || OB_ISNULL(row)) {
    is_locked = false;
    return 0;
  }

  ObMvccTransNode *head = row->get_list_head();
  if (head != nullptr && !head->is_committed() && !head->is_aborted()) {
    is_locked     = true;
    lock_trans_id = head->tx_id_;
  } else {
    is_locked = false;
  }
  return 0;
}

}  // namespace memtable
}  // namespace oceanbase
