/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/blockblocksstable/ob_data_store_desc.h */

#pragma once

#include <stdint.h>
#include "storage/ob_define.h"

namespace oceanbase {
namespace blocksstable {

using storage::ObMergeType;

// RowStoreType — simplified from OB 4.4.2
enum class ObRowStoreType : uint8_t
{
  FLAT_ROW_STORE   = 0,
  ENCODING_ROW_STORE = 1,
  MAX_ROW_STORE    = 2,
};

/**
 * ObDataStoreDesc — storage description for building SSTables.
 * Simplified from OB 4.4.2: removed compression, encryption, column group info.
 */
struct ObDataStoreDesc
{
  int64_t           tablet_id_;
  int64_t           column_count_;
  int64_t           rowkey_column_count_;
  int64_t           macro_block_size_;        // default 2MB
  int64_t           micro_block_size_;        // default 256KB
  int64_t           schema_version_;
  int64_t           snapshot_version_;
  ObRowStoreType    row_store_type_;          // FLAT only for MiniOB
  ObMergeType       merge_type_;

  ObDataStoreDesc()
    : tablet_id_(0),
      column_count_(0),
      rowkey_column_count_(0),
      macro_block_size_(DEFAULT_MACRO_BLOCK_SIZE),
      micro_block_size_(DEFAULT_MICRO_BLOCK_SIZE),
      schema_version_(0),
      snapshot_version_(0),
      row_store_type_(ObRowStoreType::FLAT_ROW_STORE),
      merge_type_(ObMergeType::MINI_MERGE) {}
};

}  // namespace blocksstable
}  // namespace oceanbase
