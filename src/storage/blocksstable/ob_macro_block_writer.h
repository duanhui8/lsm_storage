/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/blockblocksstable/ob_macro_block_writer.h */

#pragma once

#include <vector>
#include <string>

#include "storage/blocksstable/ob_macro_block.h"
#include "storage/blocksstable/ob_micro_block_writer.h"
#include "storage/blocksstable/ob_data_store_desc.h"

namespace oceanbase {
namespace blocksstable {

class ObDataIndexBlockBuilder;

/**
 * ObMacroBlockWriter — builds macro blocks from rows via micro blocks.
 * Double-buffered: ObMacroBlock macro_blocks_[2].
 * Simplified from OB 4.4.2 ob_macro_block_writer.h.
 */
class ObMacroBlockWriter
{
public:
  ObMacroBlockWriter();
  ~ObMacroBlockWriter();

  int open(const ObDataStoreDesc &desc, int64_t parallel_idx);

  /** Append a row. Returns the macro block if one was flushed. */
  int append_row(const storage::ObStoreRow &row,
                 std::string *flushed_macro_block,
                 int64_t      *macro_seq);

  /** Flush partial macro block and close. */
  int close(std::vector<std::string> &out_macro_blocks,
            std::vector<int64_t>     &out_macro_seqs);

  int64_t get_row_count() const { return total_row_count_; }

private:
  int flush_current_macro_(std::string &out_block, int64_t &out_seq);
  int build_micro_writer_();

  const ObDataStoreDesc *desc_;
  ObMicroBlockWriter     micro_writer_;
  ObMacroBlock           macro_blocks_[2];
  int                    current_macro_idx_;
  int64_t                macro_seq_;
  int64_t                total_row_count_;
  bool                   is_opened_;
};

}  // namespace blocksstable
}  // namespace oceanbase
