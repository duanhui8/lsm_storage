/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/memtable/ob_memtable.cpp */

#include <cstring>
#include <cstdio>

#include "storage/memtable/ob_memtable.h"
#include "storage/logservice/ob_log_handler.h"
#include "storage/logservice/palf/lsn.h"

namespace oceanbase {
namespace memtable {

ObMemtable::ObMemtable()
    : tablet_id_(0),
      is_inited_(false),
      is_frozen_(false),
      row_count_(0),
      occupy_size_(0),
      snapshot_version_(0),
      max_trans_version_(0),
      current_chunk_offset_(0),
      mvcc_engine_(nullptr),
      log_handler_(nullptr)
{
}

ObMemtable::~ObMemtable()
{
  delete mvcc_engine_;
  mvcc_engine_ = nullptr;
  for (char *chunk : chunks_) {
    delete[] chunk;
  }
  chunks_.clear();
}

int ObMemtable::init(int64_t tablet_id)
{
  tablet_id_ = tablet_id;

  // Allocate and initialize the MVCC engine
  mvcc_engine_ = new ObMvccEngine();
  int ret = mvcc_engine_->init(&query_engine_, this);
  if (ret != 0) {
    return ret;
  }

  is_inited_ = true;
  return 0;  // OB_SUCCESS
}

char *ObMemtable::alloc(int64_t size)
{
  std::lock_guard<std::mutex> guard(alloc_mutex_);
  if (chunks_.empty() || current_chunk_offset_ + size > CHUNK_SIZE) {
    char *new_chunk = new char[CHUNK_SIZE];
    if (OB_ISNULL(new_chunk)) {
      return nullptr;
    }
    chunks_.push_back(new_chunk);
    current_chunk_offset_ = 0;
  }
  char *ptr = chunks_.back() + current_chunk_offset_;
  current_chunk_offset_ += size;
  return ptr;
}

// ========== Write path: ObMemtable::set (from OB 4.4.2 ob_memtable.cpp) ==========

int ObMemtable::set(const storage::ObStoreCtx  &ctx,
                     const storage::ObStoreRow  &row)
{
  if (!is_inited_ || is_frozen_) {
    return -1;  // OB_NOT_INIT
  }

  // 1. Serialize row data for WAL: [dml_flag 1B][key_len 4B][key][val_len 4B][value]
  const storage::ObStoreRowkey &rk = row.rowkey_;
  int64_t key_len = rk.get_length();
  int64_t val_len = row.row_value_.size();
  int64_t log_buf_len = 1 + 4 + key_len + 4 + val_len;
  char *log_buf = static_cast<char *>(std::malloc(log_buf_len));
  if (nullptr == log_buf) return -1;
  char *p = log_buf;
  *p++ = static_cast<char>(row.dml_flag_);
  std::memcpy(p, &key_len, 4); p += 4;
  if (key_len > 0 && rk.get_data() != nullptr) {
    std::memcpy(p, rk.get_data(), key_len); p += key_len;
  }
  std::memcpy(p, &val_len, 4); p += 4;
  if (val_len > 0) {
    std::memcpy(p, row.row_value_.data(), val_len);
  }

  // 2. Write to CLOG before memtable (WAL)
  if (log_handler_ != nullptr) {
    oceanbase::palf::LSN lsn;
    int64_t scn = 0;
    int ret = log_handler_->append(log_buf, log_buf_len,
        logservice::TABLET_OP_LOG_BASE_TYPE, lsn, scn);
    if (ret != 0) { std::free(log_buf); return ret; }
  }
  std::free(log_buf);

  // 3. Generate memtable key from row
  ObMemtableKey key;
  ObMemtableKeyGenerator::generate_memtable_key(row, key);

  // 2. Build tx node arg
  ObMemtableData mtd;
  int64_t row_value_size = row.row_value_.size();
  mtd.set(row.dml_flag_, row_value_size, row.row_value_.data());

  ObTxNodeArg tx_node_arg(ctx.tx_id_, &mtd, 0 /* column_cnt — simplified */);

  // 3. Core mvcc write
  ObMvccWriteResult res;
  int ret = mvcc_write_(ctx, key, tx_node_arg, true /* check_exist */, res);
  if (ret != 0) {
    return ret;
  }

  row_count_.fetch_add(1);
  occupy_size_.fetch_add(mtd.size());
  if (ctx.snapshot_version_ > max_trans_version_.load()) {
    max_trans_version_.store(ctx.snapshot_version_);
  }

  return 0;
}

int ObMemtable::multi_set(const storage::ObStoreCtx              &ctx,
                           const std::vector<storage::ObStoreRow> &rows)
{
  for (const auto &row : rows) {
    int ret = set(ctx, row);
    if (ret != 0) {
      return ret;
    }
  }
  return 0;
}

int ObMemtable::mvcc_write_(const storage::ObStoreCtx  &ctx,
                             const ObMemtableKey         &key,
                             const ObTxNodeArg           &tx_node_arg,
                             bool                         check_exist,
                             ObMvccWriteResult           &res)
{
  // create_kv: get or create ObMvccRow in query engine
  ObMvccRow *value  = nullptr;
  ObMemtableKey stored_key;
  int ret = mvcc_engine_->create_kv(&key, &stored_key, value);
  if (ret != 0 || OB_ISNULL(value)) {
    return -1;
  }

  // mvcc_write: insert trans node into version chain
  storage::ObStoreCtx mutable_ctx = ctx;  // copy — OB does this
  ret = mvcc_engine_->mvcc_write(mutable_ctx, *value, tx_node_arg, check_exist, res);
  if (ret != 0) {
    return ret;
  }

  // ensure_kv: make sure btree has the key (for range scan)
  mvcc_engine_->ensure_kv(&stored_key, value);

  // finish_kv: make node visible
  mvcc_engine_->finish_kv(res);

  res.mtk_ = stored_key;
  return 0;
}

// ========== Read path (from OB 4.4.2 ObMemtable::get/scan) ==========

int ObMemtable::get(const storage::ObStoreCtx        &ctx,
                     const storage::ObStoreRowkey     &rowkey,
                     std::vector<storage::ObStoreRow> &results)
{
  ObMemtableKey key;
  key.encode(&rowkey);
  return mvcc_engine_->get(ctx, &key, results);
}

int ObMemtable::scan(const storage::ObStoreCtx        &ctx,
                      const storage::ObStoreRowkey     &start_key,
                      bool                              start_exclude,
                      const storage::ObStoreRowkey     &end_key,
                      bool                              end_exclude,
                      std::vector<storage::ObStoreRow> &results)
{
  // Direct btree scan — bypass complex iterator
  const auto &btree = query_engine_.get_btree();
  for (const auto &kv : btree) {
    ObMvccRow *row = kv.second;
    if (row == nullptr || row->is_empty()) continue;

    ObMvccTransNode *node = row->get_list_head();
    while (node != nullptr) {
      if (node->is_committed() || (node->flag_ & 0x01)) {
        const ObMemtableDataHeader *hdr = reinterpret_cast<const ObMemtableDataHeader *>(node->buf_);
        storage::ObStoreRow srow;
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
  return 0;
}

// ========== Replay (ObIReplaySubHandler) ==========

int ObMemtable::replay(const void *buffer, int64_t nbytes,
                        const palf::LSN &lsn, int64_t scn)
{
  (void)lsn; (void)scn;
  if (buffer == nullptr || nbytes <= 0) return -1;

  // Deserialize: [dml_flag 1B][key_len 4B][key][val_len 4B][value]
  const char *p = static_cast<const char *>(buffer);
  storage::ObDmlFlag dml = static_cast<storage::ObDmlFlag>(*p++);
  int64_t key_len; std::memcpy(&key_len, p, 4); p += 4;
  storage::ObStoreRowkey rowkey(p, key_len); p += key_len;
  int64_t val_len; std::memcpy(&val_len, p, 4); p += 4;

  storage::ObStoreRow row;
  row.dml_flag_ = static_cast<blocksstable::ObDmlFlag>(dml);
  row.rowkey_   = rowkey;
  row.row_value_.assign(p, p + val_len);
  row.trans_version_ = 1;

  // Direct write without WAL (we're replaying — already in WAL)
  // Use a null log handler temporarily to avoid recursion
  logservice::ObLogHandler *saved_handler = log_handler_;
  log_handler_ = nullptr;
  int ret = set(storage::ObStoreCtx(), row);
  log_handler_ = saved_handler;
  return ret;
}

void ObMemtable::set_log_handler(logservice::ObLogHandler *log_handler)
{
  log_handler_ = log_handler;
  if (log_handler_ != nullptr) {
    log_handler_->register_replay_handler(logservice::TABLET_OP_LOG_BASE_TYPE, this);
  }
}

}  // namespace memtable
}  // namespace oceanbase
