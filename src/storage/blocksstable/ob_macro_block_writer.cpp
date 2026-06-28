/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include <cstring>

#include "storage/blocksstable/ob_macro_block_writer.h"

namespace oceanbase {
namespace blocksstable {

ObMacroBlockWriter::ObMacroBlockWriter()
  : desc_(nullptr), current_macro_idx_(0), macro_seq_(0),
    total_row_count_(0), is_opened_(false) {}

ObMacroBlockWriter::~ObMacroBlockWriter() = default;

int ObMacroBlockWriter::open(const ObDataStoreDesc &desc, int64_t parallel_idx)
{
  desc_       = &desc;
  macro_seq_  = parallel_idx * 1000000 + 1;  // simple sequence generator

  int ret = build_micro_writer_();
  if (ret != 0) return ret;

  ret = macro_blocks_[0].init(desc, macro_seq_++);
  if (ret != 0) return ret;

  ret = macro_blocks_[1].init(desc, macro_seq_++);
  if (ret != 0) return ret;

  current_macro_idx_ = 0;
  total_row_count_   = 0;
  is_opened_         = true;
  return 0;
}

int ObMacroBlockWriter::build_micro_writer_()
{
  return micro_writer_.init(*desc_);
}

int ObMacroBlockWriter::append_row(const storage::ObStoreRow &row,
                                    std::string *flushed_macro_block,
                                    int64_t      *macro_seq)
{
  if (!is_opened_) return -1;

  int ret = micro_writer_.append_row(row);
  if (ret != 0) return ret;
  total_row_count_++;

  // Update macro block row count
  macro_blocks_[current_macro_idx_].add_row_count(1);

  // Check if micro block is full
  if (micro_writer_.is_block_full()) {
    std::string micro_data;
    ret = micro_writer_.build_block(micro_data);
    if (ret != 0) return ret;

    int64_t micro_offset = 0;
    ret = macro_blocks_[current_macro_idx_].write_micro_block(
        micro_data.data(), micro_data.size(), micro_offset);
    if (ret != 0) return ret;

    macro_blocks_[current_macro_idx_].inc_micro_block_count();
  }

  // Check if macro block is full
  int64_t remain = macro_blocks_[current_macro_idx_].get_remain_size();
  if (remain < static_cast<int64_t>(desc_->micro_block_size_)) {
    // Flush micro writer first
    if (micro_writer_.get_row_count() > 0) {
      std::string micro_data;
      ret = micro_writer_.build_block(micro_data);
      if (ret != 0) return ret;

      int64_t micro_offset = 0;
      macro_blocks_[current_macro_idx_].write_micro_block(
          micro_data.data(), micro_data.size(), micro_offset);
      macro_blocks_[current_macro_idx_].inc_micro_block_count();
    }

    return flush_current_macro_(*flushed_macro_block, *macro_seq);
  }

  if (flushed_macro_block) flushed_macro_block->clear();
  return 0;
}

int ObMacroBlockWriter::flush_current_macro_(std::string &out_block, int64_t &out_seq)
{
  ObMacroBlock &mb = macro_blocks_[current_macro_idx_];
  mb.build_common_header();

  // Assemble: CommonHeader + MacroBlockHeader + data
  int64_t total = mb.get_total_size();
  out_block.resize(total);
  int64_t pos = 0;

  mb.get_common_header().serialize(&out_block[0], total, pos);
  mb.get_macro_header().serialize(&out_block[0], total, pos);

  // Copy data
  int64_t data_size = mb.get_data_size();
  std::memcpy(&out_block[0] + pos, mb.get_data_buf(), data_size);
  pos += data_size;

  out_seq = mb.get_current_macro_seq();

  // Switch to other buffer
  current_macro_idx_ = 1 - current_macro_idx_;
  macro_blocks_[current_macro_idx_].init(*desc_, macro_seq_++);

  return 0;
}

int ObMacroBlockWriter::close(std::vector<std::string> &out_macro_blocks,
                               std::vector<int64_t>     &out_macro_seqs)
{
  if (!is_opened_) return -1;

  // Flush remaining micro block
  if (micro_writer_.get_row_count() > 0) {
    std::string micro_data;
    micro_writer_.build_block(micro_data);

    int64_t micro_offset = 0;
    macro_blocks_[current_macro_idx_].write_micro_block(
        micro_data.data(), micro_data.size(), micro_offset);
    macro_blocks_[current_macro_idx_].inc_micro_block_count();
  }

  // Flush current macro block
  std::string last_block;
  int64_t     last_seq = 0;
  flush_current_macro_(last_block, last_seq);
  out_macro_blocks.push_back(last_block);
  out_macro_seqs.push_back(last_seq);

  is_opened_ = false;
  return 0;
}

}  // namespace blocksstable
}  // namespace oceanbase
