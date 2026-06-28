/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/tablet/ob_tablet_table_store.h */

#pragma once

#include <vector>
#include <mutex>

#include "storage/ob_i_table.h"
#include "storage/ls/ob_memtable_mgr.h"
#include "storage/blocksstable/ob_sstable.h"

namespace oceanbase {
namespace logservice {
class ObLogHandler;
}

namespace storage {

/**
 * ObTableStore — manages all tables (memtables + sstables) for a tablet.
 * Provides unified read interface: active memtable → frozen → mini → minor → major.
 * Simplified from OB 4.4.2 tablet/ob_tablet_table_store.h.
 */
class ObTableStore
{
public:
  ObTableStore();
  ~ObTableStore();

  int init(int64_t tablet_id);

  // ---- MemTable access ----
  ObMemTableMgr *get_memtable_mgr() { return &memtable_mgr_; }
  memtable::ObMemtable *get_active_memtable();

  // ---- SSTable management ----
  int add_sstable(blocksstable::ObSSTable *sstable);
  int remove_sstable(blocksstable::ObSSTable *sstable);

  const std::vector<blocksstable::ObSSTable *> &get_sstables() const { return sstables_; }

  // ---- Log handler ----
  void set_log_handler(logservice::ObLogHandler *log_handler) { log_handler_ = log_handler; }
  logservice::ObLogHandler *get_log_handler() { return log_handler_; }

  // ---- Unified read (from OB 4.4.2) ----
  int get(const ObStoreCtx               &ctx,
          const ObStoreRowkey            &rowkey,
          std::vector<ObStoreRow>        &results);

  int scan(const ObStoreCtx        &ctx,
           const ObStoreRowkey     &start_key,
           bool                     start_exclude,
           const ObStoreRowkey     &end_key,
           bool                     end_exclude,
           std::vector<ObStoreRow> &results);

  // ---- Accessors ----
  int64_t get_tablet_id() const { return tablet_id_; }

private:
  int64_t                          tablet_id_;
  ObMemTableMgr                    memtable_mgr_;
  std::vector<blocksstable::ObSSTable *> sstables_;
  logservice::ObLogHandler        *log_handler_;
  mutable std::mutex               mutex_;
  bool                             is_inited_;
};

}  // namespace storage
}  // namespace oceanbase
