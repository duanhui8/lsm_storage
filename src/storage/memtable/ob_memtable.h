/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/memtable/ob_memtable.h */

/* ========== ObMemtable — 内存表(对应 OB 4.4.2 storage/memtable/ob_memtable.h)
 * OB 4.4.2: LSM-Tree 的写入口,所有写入先到 MemTable(SkipList),满后 freeze → SSTable。
 *          持有 ObMvccEngine(多版本并发控制) + ObQueryEngine(hash+btree 双索引)。
 * MiniOB: 继承 ObIMemtable + ObIReplaySubHandler,支持 PALF CLOG 回放。
 * ========== */
#pragma once

#include <atomic>
#include <vector>
#include <mutex>

#include "storage/ob_define.h"
#include "storage/ob_i_store.h"
#include "storage/memtable/ob_memtable_interface.h"
#include "storage/memtable/ob_memtable_key.h"
#include "storage/memtable/ob_memtable_data.h"
#include "storage/memtable/mvcc/ob_mvcc_engine.h"
#include "storage/memtable/mvcc/ob_mvcc_define.h"
#include "storage/memtable/mvcc/ob_query_engine.h"
#include "storage/logservice/ob_log_base_type.h"
#include "storage/logservice/ob_log_handler.h"

namespace oceanbase {
namespace logservice { class ObLogHandler; }
namespace memtable {

/**
 * ObMemtable — the main in-memory write buffer.
 * Simplified from OB 4.4.2 ob_memtable.h:184-586.
 *
 * Key members preserved from OB 4.4.2:
 *   query_engine_        — ObQueryEngine (hash + btree)
 *   mvcc_engine_         — ObMvccEngine
 *   local_allocator_     — memory allocator (simplified: heap)
 *   is_frozen_           — freeze state
 *   snapshot_version_    — freeze snapshot version
 *
 * Key methods preserved from OB 4.4.2:
 *   set()/get()/scan()   — write/read/scan
 *   is_active()/is_frozen()/set_frozen()
 */
class ObMemtable : public ObIMemtable, public logservice::ObIReplaySubHandler
{
public:
  ObMemtable();
  virtual ~ObMemtable();

  int init(int64_t tablet_id);

  // ---- ObITable interface ----
  storage::ObTableType get_table_type() const override
  {
    return storage::ObTableType::DATA_MEMTABLE;
  }
  int64_t get_row_count() const override { return row_count_.load(); }
  int64_t get_occupy_size() const override { return occupy_size_.load(); }
  bool    is_empty() const override { return row_count_.load() == 0; }

  // ---- ObIMemtable interface ----
  int64_t get_tablet_id() const override { return tablet_id_; }
  bool    is_active() const override { return !is_frozen_; }
  bool    is_frozen() const override { return is_frozen_; }
  void    set_frozen() override
  {
    is_frozen_         = true;
    snapshot_version_  = max_trans_version_.load();
  }
  int64_t get_snapshot_version() const override { return snapshot_version_; }
  int64_t get_max_merged_trans_version() const override
  {
    return max_trans_version_.load();
  }

  // ---- Write path (from OB 4.4.2 ObMemtable::set) ----

  /**
   * set — write a row into the memtable.
   * From OB 4.4.2 ObMemtable::set()
   *
   * Flow: key generation → data serialization → mvcc_write_
   */
  int set(const storage::ObStoreCtx  &ctx,
          const storage::ObStoreRow  &row);

  /**
   * multi_set — write multiple rows atomically.
   * From OB 4.4.2 ObMemtable::multi_set()
   */
  int multi_set(const storage::ObStoreCtx              &ctx,
                const std::vector<storage::ObStoreRow> &rows);

  // ---- Read path (from OB 4.4.2 ObMemtable::get/scan) ----

  /** Point get. */
  int get(const storage::ObStoreCtx               &ctx,
          const storage::ObStoreRowkey            &rowkey,
          std::vector<storage::ObStoreRow>        &results);

  /** Range scan. */
  int scan(const storage::ObStoreCtx        &ctx,
           const storage::ObStoreRowkey     &start_key,
           bool                              start_exclude,
           const storage::ObStoreRowkey     &end_key,
           bool                              end_exclude,
           std::vector<storage::ObStoreRow> &results);

  // ---- Accessors (from OB 4.4.2) ----
  ObQueryEngine &get_query_engine() { return query_engine_; }
  ObMvccEngine  &get_mvcc_engine() { return *mvcc_engine_; }

  /** Allocate memory from the memtable's arena. */
  char *alloc(int64_t size);

  // ---- Replay (ObIReplaySubHandler) ----
  int replay(const void *buffer, int64_t nbytes,
             const palf::LSN &lsn, int64_t scn) override;

  // ---- Log handler ----
  void set_log_handler(logservice::ObLogHandler *log_handler);

private:
  /** Core mvcc write path (from OB 4.4.2 ob_memtable.cpp mvcc_write_). */
  int mvcc_write_(const storage::ObStoreCtx  &ctx,
                  const ObMemtableKey         &key,
                  const ObTxNodeArg           &tx_node_arg,
                  bool                         check_exist,
                  ObMvccWriteResult           &res);

  // ---- Members (named to match OB 4.4.2 ob_memtable.h) ----
  int64_t            tablet_id_;
  bool               is_inited_;
  bool               is_frozen_;
  std::atomic<int64_t> row_count_;
  std::atomic<int64_t> occupy_size_;
  int64_t            snapshot_version_;
  std::atomic<int64_t> max_trans_version_;

  // Memory allocator — simplified from ObSingleMemstoreAllocator
  std::vector<char *>   chunks_;
  int64_t               current_chunk_offset_;
  std::mutex            alloc_mutex_;
  static const int64_t  CHUNK_SIZE = 4 * 1024 * 1024;  // 4MB per chunk

  // Core engines (same names as OB 4.4.2)
  ObQueryEngine   query_engine_;
  ObMvccEngine   *mvcc_engine_;
  logservice::ObLogHandler *log_handler_;
};

}  // namespace memtable
}  // namespace oceanbase
