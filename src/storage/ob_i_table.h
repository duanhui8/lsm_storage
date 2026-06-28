/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/ob_i_table.h */

#pragma once

#include <string>

#include "storage/ob_define.h"

namespace oceanbase {
namespace storage {

/**
 * ObITable — root interface for all table types (memtables + sstables).
 * Simplified from OB 4.4.2: removed multi_get, multi_scan, TableKey, ref counting.
 */
class ObITable
{
public:
  ObITable()          = default;
  virtual ~ObITable() = default;

  /** Get the table type. */
  virtual ObTableType get_table_type() const = 0;

  /** Number of rows in this table (approximate for memtable). */
  virtual int64_t get_row_count() const = 0;

  /** Occupied size in bytes (approximate). */
  virtual int64_t get_occupy_size() const = 0;

  /** Whether this table contains any rows. */
  virtual bool is_empty() const = 0;

  /** Whether this table is a memtable. */
  bool is_memtable() const
  {
    return get_table_type() == ObTableType::DATA_MEMTABLE ||
           get_table_type() == ObTableType::TX_DATA_MEMTABLE;
  }

  /** Whether this table is an sstable. */
  bool is_sstable() const
  {
    ObTableType t = get_table_type();
    return t == ObTableType::MINI_SSTABLE ||
           t == ObTableType::MINOR_SSTABLE ||
           t == ObTableType::MAJOR_SSTABLE;
  }

  /** Whether this is a major sstable. */
  bool is_major_sstable() const
  {
    return get_table_type() == ObTableType::MAJOR_SSTABLE;
  }

  /** Whether this is a mini sstable (single memtable dump). */
  bool is_mini_sstable() const
  {
    return get_table_type() == ObTableType::MINI_SSTABLE;
  }

  /** Whether this is a minor sstable (merged mini sstables). */
  bool is_minor_sstable() const
  {
    return get_table_type() == ObTableType::MINOR_SSTABLE;
  }
};

}  // namespace storage
}  // namespace oceanbase
