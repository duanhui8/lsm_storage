/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/memtable/mvcc/ob_mvcc_engine.h */

/* ========== ObMvccEngine — MVCC 读写协调器(对应 OB 4.4.2 storage/memtable/mvcc/ob_mvcc_engine.h)
 * OB 4.4.2: 管理每行的版本链(ObMvccRow→ObMvccTransNode 链表),
 *          提供快照读(snapshot_version)和写冲突检测(write-write conflict)。
 * MiniOB: 简化版,保留核心方法 create_kv/mvcc_write/get/scan/finish_kv。
 * ========== */
#pragma once

#include <cstdlib>
#include <cstring>

#include "storage/ob_define.h"
#include "storage/ob_i_store.h"
#include "storage/memtable/ob_memtable_key.h"
#include "storage/memtable/ob_memtable_data.h"
#include "storage/memtable/mvcc/ob_mvcc_row.h"
#include "storage/memtable/mvcc/ob_mvcc_define.h"
#include "storage/memtable/mvcc/ob_query_engine.h"

namespace oceanbase {
namespace memtable {

class ObMemtable;

/**
 * ObMvccEngine — MVCC read/write coordinator.
 * Simplified from OB 4.4.2 ob_mvcc_engine.h:54-167.
 *
 * Key methods preserved from OB 4.4.2:
 *   init(), destroy()
 *   create_kv(), create_kvs(), ensure_kv()  — key-value creation
 *   mvcc_write(), mvcc_undo(), finish_kv()  — write operations
 *   get(), scan()                            — read operations
 */
class ObMvccEngine
{
public:
  ObMvccEngine();
  virtual ~ObMvccEngine();

  virtual int init(ObQueryEngine *query_engine, ObMemtable *memtable);
  virtual void destroy();

  // ========== Read interface (from OB 4.4.2 ob_mvcc_engine.h:66-91) ==========

  /** Point get: return an iterator over visible versions for this key. */
  int get(const storage::ObStoreCtx &ctx,
          const ObMemtableKey        *parameter_key,
          std::vector<storage::ObStoreRow> &results);

  /** Range scan. */
  int scan(const storage::ObStoreCtx &ctx,
           const ObMemtableKey        *start_key,
           bool                        start_exclude,
           const ObMemtableKey        *end_key,
           bool                        end_exclude,
           std::vector<storage::ObStoreRow> &results);

  /** Check if a row is locked by an in-flight transaction. */
  int check_row_locked(const storage::ObStoreCtx &ctx,
                       const ObMemtableKey        *key,
                       bool                       &is_locked,
                       storage::ObTransID         &lock_trans_id);

  // ========== Write interface (from OB 4.4.2 ob_mvcc_engine.h:93-140) ==========

  /** Undo a write — remove the newly written tx node. */
  void mvcc_undo(ObMvccRow *value);

  /** Make the tx node visible to outer reads. */
  void finish_kv(ObMvccWriteResult &res);

  /**
   * create_kv — get or create the ObMvccRow for a key.
   * From OB 4.4.2 ob_mvcc_engine.h:104-107
   */
  virtual int create_kv(const ObMemtableKey *key,
                        ObMemtableKey       *stored_key,
                        ObMvccRow           *&value);

  /** Ensure the key-value pair is in the btree (for range scan correctness). */
  virtual int ensure_kv(const ObMemtableKey *stored_key, ObMvccRow *value);

  /**
   * mvcc_write — build and insert a new ObMvccTransNode at the head of the version chain.
   * From OB 4.4.2 ob_mvcc_engine.h:125-136
   */
  int mvcc_write(storage::ObStoreCtx &ctx,
                 ObMvccRow           &value,
                 const ObTxNodeArg   &arg,
                 const bool           check_exist,
                 ObMvccWriteResult   &res);

protected:
  /** Core create_kv logic (from OB 4.4.2 mvcc_write_ path). */
  int create_kv_(const ObMemtableKey  *key,
                 ObMemtableKey        *stored_key,
                 ObMvccRow            *&value);

  /** Query engine lookup. */
  virtual int query_engine_get_(const ObMemtableKey *parameter_key,
                                ObMvccRow           *&row,
                                ObMemtableKey       *returned_key);

  /** Build a trans node from args and allocate memory. */
  int build_tx_node_(const ObTxNodeArg &arg, ObMvccTransNode *&node);

protected:
  bool             is_inited_;
  ObQueryEngine   *query_engine_;
  ObMemtable      *memtable_;
};

}  // namespace memtable
}  // namespace oceanbase
