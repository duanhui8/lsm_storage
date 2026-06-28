/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/tablet/ob_tablet_memtable_mgr.h */

#pragma once

#include <vector>
#include <memory>
#include <mutex>

#include "storage/memtable/ob_memtable.h"

namespace oceanbase {
namespace storage {

/**
 * ObMemTableMgr — manages active and frozen memtables for a tablet.
 * Simplified from OB 4.4.2 ObTabletMemtableMgr.
 */
class ObMemTableMgr
{
public:
  ObMemTableMgr();
  ~ObMemTableMgr();

  int init(int64_t tablet_id);

  /** Get the active (writable) memtable. Creates one if needed. */
  memtable::ObMemtable *get_active_memtable();

  /** Freeze the active memtable — move to frozen list, create new active. */
  int set_is_tablet_freeze();

  /** Get all frozen memtables. */
  const std::vector<memtable::ObMemtable *> &get_frozen_memtables() const;

  /** Get all memtables (active + frozen) for read. */
  void get_all_memtables(std::vector<memtable::ObMemtable *> &out) const;

  /** Get total occupied size of all memtables. */
  int64_t get_total_occupied_size() const;

  /** Remove and destroy a frozen memtable (after dump to SSTable). */
  int remove_frozen_memtable(memtable::ObMemtable *mt);

private:
  int64_t                         tablet_id_;
  memtable::ObMemtable           *active_memtable_;
  std::vector<memtable::ObMemtable *> frozen_memtables_;
  mutable std::mutex              mutex_;
  bool                            is_inited_;
};

}  // namespace storage
}  // namespace oceanbase
