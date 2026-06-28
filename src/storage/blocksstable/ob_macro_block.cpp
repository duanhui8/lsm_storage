/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include <cstring>

#include "storage/blocksstable/ob_macro_block.h"

namespace oceanbase {
namespace blocksstable {

ObMacroBlock::ObMacroBlock()
  : spec_(nullptr), data_buf_(nullptr), macro_size_(0), data_size_(0),
    cur_macro_seq_(0), row_count_(0), micro_block_count_(0),
    max_merged_trans_version_(0), contain_uncommitted_row_(false), is_inited_(false) {}

ObMacroBlock::~ObMacroBlock()
{
  delete[] data_buf_;
  data_buf_   = nullptr;
  is_inited_  = false;
}

int ObMacroBlock::init(const ObDataStoreDesc &spec, int64_t cur_macro_seq)
{
  spec_            = &spec;
  cur_macro_seq_   = cur_macro_seq;
  macro_size_      = spec.macro_block_size_;
  data_size_       = HEADER_RESERVE;  // Reserve space for headers
  row_count_       = 0;
  micro_block_count_ = 0;

  data_buf_ = new char[macro_size_];
  if (OB_ISNULL(data_buf_)) {
    return -1;
  }

  // Init macro block header with fixed values
  macro_header_.fixed_header_.header_size_     = sizeof(ObSSTableMacroBlockHeader::FixedHeader);
  macro_header_.fixed_header_.version_         = SSTABLE_MACRO_BLOCK_HEADER_VERSION_V2;
  macro_header_.fixed_header_.magic_           = SSTABLE_MACRO_BLOCK_HEADER_MAGIC;
  macro_header_.fixed_header_.tablet_id_       = spec.tablet_id_;
  macro_header_.fixed_header_.column_count_    = static_cast<int32_t>(spec.column_count_);
  macro_header_.fixed_header_.rowkey_column_count_ = static_cast<int32_t>(spec.rowkey_column_count_);
  macro_header_.fixed_header_.row_store_type_  = static_cast<int32_t>(ObRowStoreType::FLAT_ROW_STORE);

  std::memset(data_buf_, 0, macro_size_);
  is_inited_ = true;
  return 0;
}

int ObMacroBlock::write_micro_block(const char *micro_data, int64_t micro_size,
                                     int64_t &data_offset)
{
  if (!is_inited_ || OB_ISNULL(micro_data)) return -1;
  if (data_size_ + micro_size > macro_size_) return -1;

  data_offset = data_size_;
  std::memcpy(data_buf_ + data_size_, micro_data, micro_size);
  data_size_ += micro_size;
  return 0;
}

int ObMacroBlock::build_common_header()
{
  macro_header_.fixed_header_.row_count_           = row_count_;
  macro_header_.fixed_header_.occupy_size_         = static_cast<int32_t>(data_size_);
  macro_header_.fixed_header_.micro_block_count_   = micro_block_count_;
  macro_header_.fixed_header_.micro_block_data_offset_ = HEADER_RESERVE;
  macro_header_.fixed_header_.micro_block_data_size_   = static_cast<int32_t>(data_size_ - HEADER_RESERVE);

  // Construct common header
  common_header_.header_size_  = sizeof(ObMacroBlockCommonHeader);
  common_header_.version_      = MACRO_BLOCK_COMMON_HEADER_VERSION;
  common_header_.magic_        = MACRO_BLOCK_COMMON_HEADER_MAGIC;
  common_header_.attr_         = static_cast<int32_t>(MacroBlockType::SSTableData);
  common_header_.payload_size_ = static_cast<int32_t>(data_size_);
  common_header_.payload_checksum_ = 0;  // simplified — no checksum for MiniOB

  is_inited_ = false;
  return 0;
}

int64_t ObMacroBlock::get_total_size() const
{
  return sizeof(ObMacroBlockCommonHeader) +
         sizeof(ObSSTableMacroBlockHeader::FixedHeader) +
         data_size_;
}

}  // namespace blocksstable
}  // namespace oceanbase
