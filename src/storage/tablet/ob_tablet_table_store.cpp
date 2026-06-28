/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved. miniob is licensed under Mulan PSL v2. */

#include "storage/tablet/ob_tablet_table_store.h"

namespace oceanbase {
namespace storage {

ObTableStore::ObTableStore() : tablet_id_(0), log_handler_(nullptr), is_inited_(false) {}
ObTableStore::~ObTableStore()
{
  for (auto *sst : sstables_) delete sst;
}

int ObTableStore::init(int64_t tablet_id)
{
  tablet_id_ = tablet_id;
  memtable_mgr_.init(tablet_id);
  is_inited_ = true;
  return 0;
}

memtable::ObMemtable *ObTableStore::get_active_memtable()
{
  return memtable_mgr_.get_active_memtable();
}

int ObTableStore::add_sstable(blocksstable::ObSSTable *sstable)
{
  std::lock_guard<std::mutex> guard(mutex_);
  sstables_.push_back(sstable);
  return 0;
}

int ObTableStore::remove_sstable(blocksstable::ObSSTable *sstable)
{
  std::lock_guard<std::mutex> guard(mutex_);
  for (auto it = sstables_.begin(); it != sstables_.end(); ++it) {
    if (*it == sstable) {
      delete *it;
      sstables_.erase(it);
      return 0;
    }
  }
  return -1;
}

int ObTableStore::get(const ObStoreCtx        &ctx,
                       const ObStoreRowkey     &rowkey,
                       std::vector<ObStoreRow> &results)
{
  results.clear();

  // 1. Check active memtable first (newest)
  memtable::ObMemtable *active = memtable_mgr_.get_active_memtable();
  if (active && !active->is_empty()) {
    int ret = active->get(ctx, rowkey, results);
    if (ret == 0 && !results.empty()) return 0;
  }

  // 2. Check frozen memtables
  for (auto *mt : memtable_mgr_.get_frozen_memtables()) {
    if (mt && !mt->is_empty()) {
      int ret = mt->get(ctx, rowkey, results);
      if (ret == 0 && !results.empty()) return 0;
    }
  }

  // 3. Check SSTables (newest first — mini → minor → major)
  // Simplified: iterate all SSTables
  for (auto *sst : sstables_) {
    if (sst && !sst->is_empty()) {
      // For now, SSTable read needs an iterator
      // Simplified: just check if results accumulate
    }
  }

  return -1;  // Not found
}

int ObTableStore::scan(const ObStoreCtx        &ctx,
                        const ObStoreRowkey     &start_key,
                        bool                     start_exclude,
                        const ObStoreRowkey     &end_key,
                        bool                     end_exclude,
                        std::vector<ObStoreRow> &results)
{
  results.clear();

  // Scan active memtable
  memtable::ObMemtable *active = memtable_mgr_.get_active_memtable();
  if (active) {
    active->scan(ctx, start_key, start_exclude, end_key, end_exclude, results);
  }

  // Scan frozen memtables
  for (auto *mt : memtable_mgr_.get_frozen_memtables()) {
    if (mt) {
      std::vector<ObStoreRow> tmp;
      mt->scan(ctx, start_key, start_exclude, end_key, end_exclude, tmp);
      results.insert(results.end(), tmp.begin(), tmp.end());
    }
  }

  return 0;
}

}  // namespace storage
}  // namespace oceanbase
