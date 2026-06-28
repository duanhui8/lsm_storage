/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/blockblocksstable/ob_micro_block_writer.h */

#pragma once

#include <string>
#include <vector>

#include "storage/blocksstable/ob_block_header.h"
#include "storage/blocksstable/ob_data_store_desc.h"
#include "storage/ob_i_store.h"

namespace oceanbase {
namespace blocksstable {

/**
 * ObMicroBlockWriter — builds micro blocks from rows in FLAT format.
 * Simplified from OB 4.4.2 ob_micro_block_writer.h.
 *
 * Build flow: append_row() repeatedly → build_block() when full.
 */
class ObMicroBlockWriter
{
public:
  ObMicroBlockWriter();
  ~ObMicroBlockWriter();

  int init(const ObDataStoreDesc &desc);

  /** Append a row (serialized bytes) to the current micro block. */
  int append_row(const storage::ObStoreRow &row);

  /** Build the current micro block and return the compressed data. */
  int build_block(std::string &out_block_data);

  /** Check if the block has enough space for another row. */
  bool is_block_full() const { return current_size_ >= micro_block_size_ - 4096; }

  /** Get row count in current block. */
  int32_t get_row_count() const { return row_count_; }

  /** Reset for a new block. */
  void reset();

  /** Get the last row key written (for index). */
  const storage::ObStoreRowkey &get_last_rowkey() const { return last_rowkey_; }

private:
  int write_row_header_(const storage::ObStoreRow &row);

  const ObDataStoreDesc *desc_;
  std::string            data_buf_;       // header + row data area
  std::vector<int32_t>   row_offsets_;    // offset of each row from data start
  int64_t                micro_block_size_;
  int64_t                current_size_;
  int32_t                row_count_;
  ObMicroBlockHeader     header_;
  storage::ObStoreRowkey last_rowkey_;
};

}  // namespace blocksstable
}  // namespace oceanbase
