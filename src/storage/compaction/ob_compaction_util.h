/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/compaction/ob_compaction_util.h */

#pragma once

#include "storage/ob_define.h"

namespace oceanbase {
namespace compaction {

// Merge types already defined in storage namespace — alias here for OB 4.4.2 naming
using ObMergeType = storage::ObMergeType;

static const int64_t MINI_MERGE_LEVEL       = 1;
static const int64_t MINOR_MERGE_LEVEL      = 2;
static const int64_t MAJOR_MERGE_LEVEL      = 3;
static const int64_t MINI_SSTABLE_TRIGGER   = 4;   // trigger MINOR after 4 mini sstables
static const int64_t MINOR_SSTABLE_TRIGGER  = 8;   // trigger MAJOR after 8 minor sstables

}  // namespace compaction
}  // namespace oceanbase
