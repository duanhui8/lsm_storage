/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/memtable/ob_memtable_interface.h */

#pragma once

#include "storage/ob_i_table.h"

namespace oceanbase {
namespace memtable {

/**
 * ObIMemtable — interface for all memtable types.
 * Simplified from OB 4.4.2 ob_memtable_interface.h:110-170
 */
class ObIMemtable : public storage::ObITable
{
public:
  ObIMemtable()          = default;
  virtual ~ObIMemtable() = default;

  /** Get the tablet id this memtable belongs to. */
  virtual int64_t get_tablet_id() const = 0;

  /** Whether this memtable is active (accepting writes). */
  virtual bool is_active() const = 0;

  /** Whether this memtable is frozen (ready for dump). */
  virtual bool is_frozen() const = 0;

  /** Mark this memtable as frozen. After this, no new writes are accepted. */
  virtual void set_frozen() = 0;

  /** Get the snapshot version of this memtable. */
  virtual int64_t get_snapshot_version() const = 0;

  /** Get the maximum committed transaction version in this memtable. */
  virtual int64_t get_max_merged_trans_version() const = 0;
};

}  // namespace memtable
}  // namespace oceanbase
