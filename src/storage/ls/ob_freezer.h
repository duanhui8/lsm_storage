/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/ls/ob_freezer.h */

#pragma once

#include "storage/ls/ob_memtable_mgr.h"

namespace oceanbase {
namespace storage {

/**
 * ObFreezer — triggers memtable freeze when memory threshold is exceeded.
 * Simplified from OB 4.4.2 ObFreezer.
 */
class ObFreezer
{
public:
  ObFreezer();
  ~ObFreezer();

  int init(ObMemTableMgr *mgr, int64_t memtable_size_threshold);

  /** Check and trigger freeze if needed. Returns true if freeze happened. */
  bool try_freeze();

  int64_t get_memtable_size_threshold() const { return threshold_; }

private:
  ObMemTableMgr *mgr_;
  int64_t        threshold_;
  bool           is_inited_;
};

}  // namespace storage
}  // namespace oceanbase
