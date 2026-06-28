/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#pragma once

#include <stdint.h>
#include <cstring>

// OB basic macros (from OB 4.4.2)
#define OB_ISNULL(ptr)  (nullptr == (ptr))
#define OB_NOT_NULL(ptr) (nullptr != (ptr))
#define MEMCPY(dest, src, n) std::memcpy(dest, src, n)
#define DISALLOW_COPY_AND_ASSIGN(cls) \
  cls(const cls &) = delete;          \
  cls &operator=(const cls &) = delete

namespace oceanbase {

// ---- DML flag (from OB 4.4.2 ob_i_store.h / ob_memtable_data.h) ----
namespace memtable {
enum class ObDmlFlag : int32_t
{
  DF_NOT_SET = 0,
  DF_INSERT  = 1,
  DF_UPDATE  = 2,
  DF_DELETE  = 3,
  DF_LOCK    = 4,
  DF_MAX     = 5,
};
}  // namespace memtable

namespace blocksstable {
// ObDmlFlag alias — OB 4.4.2 references blocksstable::ObDmlFlag in ObMemtableDataHeader
using ObDmlFlag = memtable::ObDmlFlag;
}  // namespace blocksstable

// ---- Error codes (use oceanbase namespace directly to avoid collision with MiniOB's common) ----
static const int OB_SUCCESS         = 0;
static const int OB_ENTRY_EXIST     = -1;
static const int OB_ENTRY_NOT_EXIST = -1;
static const int OB_ITER_END        = -1;

// ---- Block constants (from ob_macro_block_common_header.h) ----
namespace blocksstable {
static const int32_t MACRO_BLOCK_COMMON_HEADER_VERSION = 1;
static const int32_t MACRO_BLOCK_COMMON_HEADER_MAGIC   = 1001;

// Block sizes (from ob_data_store_desc.h defaults)
static const int64_t DEFAULT_MACRO_BLOCK_SIZE  = 2 * 1024 * 1024;  // 2MB
static const int64_t DEFAULT_MICRO_BLOCK_SIZE  = 256 * 1024;       // 256KB
static const int64_t MIN_MICRO_BLOCK_SIZE      = 4 * 1024;         // 4KB
static const int64_t DEFAULT_RESERVE_PERCENT   = 90;

// SSTable macro block header (from ob_sstable_macro_block_header.h)
static const uint16_t SSTABLE_MACRO_BLOCK_HEADER_MAGIC      = 1007;
static const uint16_t SSTABLE_MACRO_BLOCK_HEADER_VERSION_V2 = 2;

// Memtable defaults
static const int64_t DEFAULT_MEMTABLE_SIZE = 64 * 1024 * 1024;  // 64MB

// Max column count (simplified)
static const int64_t OB_MAX_COLUMN_NUMBER = 128;
}  // namespace blocksstable

namespace storage {

// Table types (from ob_i_table.h)
enum class ObTableType : unsigned char
{
  DATA_MEMTABLE   = 0,
  TX_DATA_MEMTABLE = 1,
  MINI_SSTABLE    = 12,
  MINOR_SSTABLE   = 11,
  MAJOR_SSTABLE   = 10,
  MAX_TABLE_TYPE  = 13,
};

// Merge types (from compaction/ob_compaction_util.h)
enum class ObMergeType : int32_t
{
  MINI_MERGE  = 0,
  MINOR_MERGE = 1,
  MAJOR_MERGE = 2,
  NOT_INIT    = 3,
};

// Transaction ID (simplified from share/scn.h and ob_trans_define.h)
using ObTransID   = int64_t;
using ObTxSEQ     = int32_t;

static const ObTransID OB_INVALID_TRANS_ID = -1;
static const int64_t   OB_INVALID_VERSION  = INT64_MAX;

}  // namespace storage
}  // namespace oceanbase
