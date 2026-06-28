/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include <cstring>

#include "storage/blocksstable/ob_micro_block_writer.h"

namespace oceanbase {
namespace blocksstable {

ObMicroBlockWriter::ObMicroBlockWriter()
  : desc_(nullptr), micro_block_size_(0), current_size_(0), row_count_(0) {}

ObMicroBlockWriter::~ObMicroBlockWriter() = default;

int ObMicroBlockWriter::init(const ObDataStoreDesc &desc)
{
  desc_             = &desc;
  micro_block_size_ = desc.micro_block_size_;
  reset();
  return 0;
}

void ObMicroBlockWriter::reset()
{
  // Reserve space for header
  header_.reset();
  header_.magic_            = ObMicroBlockHeader::MICRO_BLOCK_HEADER_MAGIC;
  header_.version_          = 1;
  header_.column_count_     = static_cast<uint16_t>(desc_->column_count_);
  header_.rowkey_column_count_ = static_cast<uint16_t>(desc_->rowkey_column_count_);
  header_.row_store_type_   = static_cast<uint8_t>(ObRowStoreType::FLAT_ROW_STORE);
  header_.single_version_rows_      = 1;
  header_.contain_uncommitted_rows_ = 0;
  header_.is_last_row_last_flag_    = 0;
  header_.is_first_row_first_flag_  = 0;

  int64_t header_size = ObMicroBlockHeader::get_serialize_size();
  data_buf_.clear();
  data_buf_.resize(header_size, '\0');  // header placeholder
  row_offsets_.clear();
  current_size_ = header_size;
  row_count_    = 0;
  last_rowkey_.reset();
}

int ObMicroBlockWriter::append_row(const storage::ObStoreRow &row)
{
  if (OB_ISNULL(desc_)) return -1;

  // Write row data into data_buf_
  int64_t row_start = data_buf_.size();

  // Simple flat row encoding: [dml_flag 1B][key_len 4B][key][val_len 4B][value]
  uint8_t  dml     = static_cast<uint8_t>(row.dml_flag_);
  int32_t  key_len = static_cast<int32_t>(row.rowkey_.get_length());
  int32_t  val_len = static_cast<int32_t>(row.row_value_.size());

  data_buf_.push_back(static_cast<char>(dml));
  data_buf_.append(reinterpret_cast<const char *>(&key_len), sizeof(key_len));
  data_buf_.append(row.rowkey_.get_data(), key_len);
  data_buf_.append(reinterpret_cast<const char *>(&val_len), sizeof(val_len));
  if (val_len > 0) {
    data_buf_.append(row.row_value_.data(), val_len);
  }

  // Record row offset from data area start
  row_offsets_.push_back(static_cast<int32_t>(row_start));
  current_size_ = data_buf_.size();
  row_count_++;

  // Track last key
  last_rowkey_ = row.rowkey_;

  return 0;
}

int ObMicroBlockWriter::build_block(std::string &out_block_data)
{
  if (row_count_ == 0) {
    out_block_data.clear();
    return 0;
  }

  // Write row index at end
  int32_t row_index_start = static_cast<int32_t>(data_buf_.size());
  for (int32_t off : row_offsets_) {
    data_buf_.append(reinterpret_cast<const char *>(&off), sizeof(off));
  }

  // Update header
  header_.row_count_       = static_cast<uint32_t>(row_count_);
  header_.row_index_offset_ = row_index_start;
  header_.data_length_     = static_cast<int32_t>(data_buf_.size());
  header_.data_zlength_    = header_.data_length_;
  header_.original_length_ = header_.data_length_;
  header_.data_checksum_   = 0;  // simplified
  header_.max_merged_trans_version_ = 0;

  // Write header into the reserved space at front
  int64_t pos = 0;
  header_.serialize(&data_buf_[0], data_buf_.size(), pos);

  out_block_data = data_buf_;
  reset();
  return 0;
}

}  // namespace blocksstable
}  // namespace oceanbase
