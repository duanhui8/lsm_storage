/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include <cstring>

#include "storage/blocksstable/ob_sstable_builder.h"

namespace oceanbase {
namespace blocksstable {

ObSSTableBuilder::ObSSTableBuilder() : is_closed_(false) {}

ObSSTableBuilder::~ObSSTableBuilder()
{
  if (!is_closed_) close();
}

int ObSSTableBuilder::init(const ObDataStoreDesc &desc,
                            const std::string     &data_file_path,
                            const std::string     &meta_file_path)
{
  desc_           = desc;
  data_file_path_ = data_file_path;
  meta_file_path_ = meta_file_path;

  int ret = writer_.open(desc, 0 /* parallel_idx */);
  if (ret != 0) return ret;

  ret = meta_.init(desc.tablet_id_, desc.column_count_,
                   desc.rowkey_column_count_,
                   desc.schema_version_, desc.snapshot_version_);
  return ret;
}

int ObSSTableBuilder::append_row(const storage::ObStoreRow &row)
{
  std::string flushed_block;
  int64_t     flushed_seq = 0;
  int ret = writer_.append_row(row, &flushed_block, &flushed_seq);
  if (ret != 0) return ret;

  if (!flushed_block.empty()) {
    macro_blocks_.push_back(flushed_block);
    macro_seqs_.push_back(flushed_seq);
  }
  return 0;
}

int ObSSTableBuilder::close()
{
  if (is_closed_) return 0;

  // Flush remaining macro blocks
  std::vector<std::string> remaining_blocks;
  std::vector<int64_t>     remaining_seqs;
  int ret = writer_.close(remaining_blocks, remaining_seqs);
  if (ret != 0) return ret;

  for (const auto &b : remaining_blocks) {
    macro_blocks_.push_back(b);
  }
  for (auto s : remaining_seqs) {
    macro_seqs_.push_back(s);
  }

  // Create SSTable
  ret = sstable_.create_from_blocks(macro_blocks_, macro_seqs_, meta_,
                                     data_file_path_, meta_file_path_);
  is_closed_ = true;
  return ret;
}

}  // namespace blocksstable
}  // namespace oceanbase
