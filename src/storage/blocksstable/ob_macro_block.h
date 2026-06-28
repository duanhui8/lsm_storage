/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/blockblocksstable/ob_macro_block.h */

#pragma once

#include <string>

#include "storage/blocksstable/ob_block_header.h"
#include "storage/blocksstable/ob_data_store_desc.h"

namespace oceanbase {
namespace blocksstable {

/**
 * ObMacroBlock — in-memory representation of a macro block during write.
 * Simplified from OB 4.4.2 ob_macro_block.h.
 */
class ObMacroBlock
{
public:
  ObMacroBlock();
  ~ObMacroBlock();

  int init(const ObDataStoreDesc &spec, int64_t cur_macro_seq);

  /** Write a micro block into this macro block at the current offset. */
  int write_micro_block(const char *micro_data, int64_t micro_size,
                        int64_t &data_offset);

  /** Get the current data size (excluding common header). */
  int64_t get_data_size() const { return data_size_; }

  /** Remaining space in this macro block. */
  int64_t get_remain_size() const { return macro_size_ - data_size_; }

  /** Get current macro sequence number. */
  int64_t get_current_macro_seq() const { return cur_macro_seq_; }

  /** Row and micro block counts. */
  int32_t get_row_count() const { return row_count_; }
  int32_t get_micro_block_count() const { return micro_block_count_; }

  /** Add rows to the count. */
  void add_row_count(int32_t n) { row_count_ += n; }
  void inc_micro_block_count() { micro_block_count_++; }

  /** Access to headers. */
  ObSSTableMacroBlockHeader &get_macro_header() { return macro_header_; }
  ObMacroBlockCommonHeader  &get_common_header() { return common_header_; }

  /** Get the data buffer for writing. */
  char *get_data_buf() { return data_buf_; }

  /** Flush — prepare for writing to disk. Returns full block data and size. */
  int build_common_header();
  const char *get_buffer() const { return data_buf_; }
  int64_t get_total_size() const;

private:
  static const int64_t HEADER_RESERVE = 4096;  // reserve 4KB for headers

  const ObDataStoreDesc *spec_;
  char                  *data_buf_;
  int64_t                macro_size_;
  int64_t                data_size_;
  int64_t                cur_macro_seq_;
  ObSSTableMacroBlockHeader macro_header_;
  ObMacroBlockCommonHeader common_header_;
  int32_t                row_count_;
  int32_t                micro_block_count_;
  int64_t                max_merged_trans_version_;
  bool                   contain_uncommitted_row_;
  bool                   is_inited_;
};

}  // namespace blocksstable
}  // namespace oceanbase
