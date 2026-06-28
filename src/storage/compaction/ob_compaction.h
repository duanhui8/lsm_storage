/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/compaction/ob_compaction.h */

#pragma once

#include <string>
#include <vector>

#include "storage/ob_i_store.h"
#include "storage/tablet/ob_tablet_table_store.h"
#include "storage/blocksstable/ob_sstable_builder.h"
#include "storage/compaction/ob_compaction_util.h"

namespace oceanbase {
namespace compaction {

/**
 * ObCompaction — performs compaction merges.
 * Simplified from OB 4.4.2.
 */
class ObCompaction
{
public:
  ObCompaction();
  ~ObCompaction();

  /**
   * Mini merge — dump a single frozen memtable into a Mini SSTable.
   */
  int mini_merge(memtable::ObMemtable         *frozen_memtable,
                 const blocksstable::ObDataStoreDesc &desc,
                 const std::string             &data_path,
                 const std::string             &meta_path,
                 blocksstable::ObSSTable       *&out_sstable);

  /**
   * Minor merge — merge multiple Mini SSTables into one Minor SSTable.
   */
  int minor_merge(const std::vector<blocksstable::ObSSTable *> &input_sstables,
                  const blocksstable::ObDataStoreDesc           &desc,
                  const std::string                             &data_path,
                  const std::string                             &meta_path,
                  blocksstable::ObSSTable                       *&out_sstable);

  /**
   * Major merge — merge Minor + existing Major into new Major.
   */
  int major_merge(const std::vector<blocksstable::ObSSTable *> &input_sstables,
                  blocksstable::ObSSTable                       *old_major,
                  const blocksstable::ObDataStoreDesc            &desc,
                  const std::string                              &data_path,
                  const std::string                              &meta_path,
                  blocksstable::ObSSTable                        *&out_sstable);

private:
  int merge_sstables_(const std::vector<blocksstable::ObSSTable *> &inputs,
                      const blocksstable::ObDataStoreDesc           &desc,
                      const std::string                             &data_path,
                      const std::string                             &meta_path,
                      blocksstable::ObSSTable                       *&out_sstable);
};

}  // namespace compaction
}  // namespace oceanbase
