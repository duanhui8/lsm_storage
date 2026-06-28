/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to:
  /opt/oceanbase4.4.2/src/storage/blockblocksstable/ob_macro_block_common_header.h
  /opt/oceanbase4.4.2/src/storage/blockblocksstable/ob_sstable_macro_block_header.h
  /opt/oceanbase4.4.2/src/storage/blockblocksstable/ob_micro_block_header.h */

#pragma once

#include <stdint.h>
#include <cstddef>
#include <cstring>

namespace oceanbase {
namespace blocksstable {

// ============================================================================
// ObMacroBlockCommonHeader — 24 bytes at start of every macro block
// From: ob_macro_block_common_header.h
// ============================================================================

enum class MacroBlockType : int32_t
{
  None              = 0,
  SSTableData       = 1,
  SSTableIndex      = 6,
  SSTableMacroMeta  = 7,
  MaxMacroType,
};

struct ObMacroBlockCommonHeader final
{
  static const int32_t MACRO_BLOCK_COMMON_HEADER_VERSION = 1;
  static const int32_t MACRO_BLOCK_COMMON_HEADER_MAGIC   = 1001;

  ObMacroBlockCommonHeader()
    : header_size_(0), version_(1), magic_(1001),
      attr_(static_cast<int32_t>(MacroBlockType::SSTableData)),
      payload_size_(0), payload_checksum_(0) {}

  void reset()
  {
    header_size_     = 0;
    version_         = 1;
    magic_           = 1001;
    attr_            = static_cast<int32_t>(MacroBlockType::SSTableData);
    payload_size_    = 0;
    payload_checksum_ = 0;
  }

  bool is_valid() const { return magic_ == MACRO_BLOCK_COMMON_HEADER_MAGIC; }
  bool is_sstable_data_block() const
  {
    return static_cast<MacroBlockType>(attr_) == MacroBlockType::SSTableData;
  }
  bool is_sstable_index_block() const
  {
    return static_cast<MacroBlockType>(attr_) == MacroBlockType::SSTableIndex;
  }

  int32_t get_payload_size() const { return payload_size_; }
  int32_t get_payload_checksum() const { return payload_checksum_; }
  void set_payload_size(int32_t sz) { payload_size_ = sz; }
  void set_payload_checksum(int32_t ck) { payload_checksum_ = ck; }
  void set_attr(MacroBlockType t) { attr_ = static_cast<int32_t>(t); }

  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const
  {
    if (buf_len - pos < static_cast<int64_t>(sizeof(*this))) return -1;
    std::memcpy(buf + pos, this, sizeof(*this));
    pos += sizeof(*this);
    return 0;
  }
  int deserialize(const char *buf, const int64_t data_len, int64_t &pos)
  {
    if (data_len - pos < static_cast<int64_t>(sizeof(*this))) return -1;
    std::memcpy(this, buf + pos, sizeof(*this));
    pos += sizeof(*this);
    return 0;
  }
  static int64_t get_serialize_size() { return sizeof(ObMacroBlockCommonHeader); }

  int32_t header_size_;
  int32_t version_;
  int32_t magic_;
  int32_t attr_;
  int32_t payload_size_;
  int32_t payload_checksum_;
};

// ============================================================================
// ObSSTableMacroBlockHeader::FixedHeader — simplified V2
// From: ob_sstable_macro_block_header.h
// ============================================================================

struct ObSSTableMacroBlockHeader final
{
  static const uint16_t SSTABLE_MACRO_BLOCK_HEADER_VERSION_V2 = 2;
  static const uint16_t SSTABLE_MACRO_BLOCK_HEADER_MAGIC      = 1007;

  struct FixedHeader
  {
    uint32_t header_size_;
    uint16_t version_;
    uint16_t magic_;
    uint64_t tablet_id_;
    int64_t  logical_version_;
    int64_t  data_seq_;
    int32_t  column_count_;
    int32_t  rowkey_column_count_;
    int32_t  row_store_type_;       // ObRowStoreType — 0=FLAT
    int32_t  row_count_;
    int32_t  occupy_size_;
    int32_t  micro_block_count_;
    int32_t  micro_block_data_offset_;
    int32_t  micro_block_data_size_;
    int32_t  idx_block_offset_;
    int32_t  idx_block_size_;
    int32_t  meta_block_offset_;
    int32_t  meta_block_size_;
    int64_t  data_checksum_;

    FixedHeader() { std::memset(this, 0, sizeof(*this)); }
    bool is_valid() const { return magic_ == SSTABLE_MACRO_BLOCK_HEADER_MAGIC; }
  };

  ObSSTableMacroBlockHeader() : column_types_(nullptr), column_orders_(nullptr),
    column_checksum_(nullptr), is_normal_cg_(true), is_inited_(false) {}

  bool is_valid() const { return fixed_header_.is_valid(); }

  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const
  {
    int64_t need = get_serialize_size();
    if (buf_len - pos < need) return -1;
    std::memcpy(buf + pos, &fixed_header_, sizeof(FixedHeader));
    pos += sizeof(FixedHeader);
    // Skip variable-length arrays (not needed for MiniOB simplified format)
    return 0;
  }

  int deserialize(const char *buf, const int64_t data_len, int64_t &pos)
  {
    if (data_len - pos < static_cast<int64_t>(sizeof(FixedHeader))) return -1;
    std::memcpy(&fixed_header_, buf + pos, sizeof(FixedHeader));
    pos += sizeof(FixedHeader);
    return 0;
  }

  int64_t get_serialize_size() const
  {
    return sizeof(FixedHeader);
  }

  FixedHeader fixed_header_;
  int64_t    *column_types_;
  int64_t    *column_orders_;
  int64_t    *column_checksum_;
  bool        is_normal_cg_;
  bool        is_inited_;
};

// ============================================================================
// ObMicroBlockHeader — row group header inside a macro block
// Simplified: flat format only, no hash index, no column checksums
// From: ob_micro_block_header.h
// ============================================================================

struct ObMicroBlockHeader
{
  static const int16_t MICRO_BLOCK_HEADER_MAGIC = 1005;

  int16_t  magic_;
  int16_t  version_;
  uint32_t header_size_;
  int16_t  header_checksum_;
  uint16_t column_count_;
  uint16_t rowkey_column_count_;
  // Bit fields
  uint16_t has_column_checksum_    : 1;
  uint16_t has_string_out_row_     : 1;
  uint16_t all_lob_in_row_         : 1;
  uint16_t contains_hash_index_    : 1;
  uint16_t hash_index_offset_      : 10;
  uint16_t reserved_               : 2;
  uint32_t row_count_;
  uint8_t  row_store_type_;
  // Flat format flags
  uint8_t  single_version_rows_    : 1;
  uint8_t  contain_uncommitted_rows_ : 1;
  uint8_t  is_last_row_last_flag_  : 1;
  uint8_t  is_first_row_first_flag_ : 1;
  uint8_t  flat_reserved_          : 4;
  uint8_t  compressor_type_;
  uint8_t  cs_reserved_;
  uint32_t row_index_offset_;      // offset to row index array (from start of data area)

  int32_t  original_length_;
  int64_t  max_merged_trans_version_;
  int32_t  data_length_;
  int32_t  data_zlength_;
  int64_t  data_checksum_;

  ObMicroBlockHeader() { std::memset(this, 0, sizeof(*this)); magic_ = 1005; version_ = 1; }

  void reset() { std::memset(this, 0, sizeof(*this)); magic_ = 1005; version_ = 1; }

  bool is_valid() const { return magic_ == MICRO_BLOCK_HEADER_MAGIC; }

  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const
  {
    int64_t sz = get_serialize_size();
    if (buf_len - pos < sz) return -1;
    std::memcpy(buf + pos, this, sz);
    pos += sz;
    return 0;
  }

  int deserialize(const char *buf, const int64_t data_len, int64_t &pos)
  {
    int64_t sz = get_serialize_size();
    if (data_len - pos < sz) return -1;
    std::memcpy(this, buf + pos, sz);
    pos += sz;
    return 0;
  }

  static uint32_t get_serialize_size()
  {
    // Fixed size without column checksums
    return offsetof(ObMicroBlockHeader, data_checksum_) + sizeof(data_checksum_);
  }
};

}  // namespace blocksstable
}  // namespace oceanbase
