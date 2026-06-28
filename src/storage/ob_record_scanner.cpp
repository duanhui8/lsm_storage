/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include "ob_record_scanner.h"
#include "common/log/log.h"

ObRecordScanner::ObRecordScanner(oceanbase::storage::ObTableStore *table_store)
    : table_store_(table_store), current_idx_(-1), is_open_(false) {}

RC ObRecordScanner::open_scan()
{
  if (nullptr == table_store_) return RC::INTERNAL;

  oceanbase::storage::ObStoreCtx ctx;
  ctx.tx_id_ = 0;
  ctx.snapshot_version_ = oceanbase::storage::OB_INVALID_VERSION;

  oceanbase::storage::ObStoreRowkey start_key("", 0);
  oceanbase::storage::ObStoreRowkey end_key("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 8);

  std::vector<oceanbase::storage::ObStoreRow> rows;
  int ret = table_store_->scan(ctx, start_key, false, end_key, false, rows);
  if (ret != 0) return RC::INTERNAL;

  for (auto &srow : rows) {
    if (srow.is_deleted_) continue;
    if (srow.row_value_.empty()) continue;
    row_data_cache_.push_back(srow.row_value_);
  }
  current_idx_ = 0;
  is_open_ = true;
  return RC::SUCCESS;
}

RC ObRecordScanner::close_scan()
{
  row_data_cache_.clear();
  current_idx_ = -1;
  is_open_ = false;
  return RC::SUCCESS;
}

RC ObRecordScanner::next_record(std::vector<char> &row_data)
{
  if (!is_open_) return RC::INTERNAL;
  if (current_idx_ >= static_cast<int>(row_data_cache_.size())) return RC::RECORD_EOF;
  row_data = row_data_cache_[current_idx_];
  current_idx_++;
  return RC::SUCCESS;
}
