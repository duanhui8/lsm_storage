/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/compaction/ob_sstable_builder.h */

#pragma once

#include <string>
#include <vector>

#include "storage/ob_i_store.h"
#include "storage/blocksstable/ob_macro_block_writer.h"
#include "storage/blocksstable/ob_sstable.h"
#include "storage/blocksstable/ob_sstable_meta.h"

namespace oceanbase {
namespace blocksstable {

/**
 * ObSSTableBuilder — builds an SSTable from a row iterator.
 * Simplified from OB 4.4.2 (compaction/ob_sstable_builder.h).
 *
 * Usage:
 *   ObSSTableBuilder builder;
 *   builder.init(desc, data_path, meta_path);
 *   while (row_iter->next()) builder.append_row(row);
 *   builder.close();
 *   ObSSTable sstable = builder.get_sstable();
 */
class ObSSTableBuilder
{
public:
  ObSSTableBuilder();
  ~ObSSTableBuilder();

  int init(const ObDataStoreDesc &desc,
           const std::string     &data_file_path,
           const std::string     &meta_file_path);

  /** Append a row to the builder. */
  int append_row(const storage::ObStoreRow &row);

  /** Close the builder and write all files. */
  int close();

  /** Get the built SSTable. */
  const ObSSTable &get_sstable() const { return sstable_; }

  int64_t get_row_count() const { return writer_.get_row_count(); }

private:
  ObDataStoreDesc         desc_;
  ObMacroBlockWriter      writer_;
  std::vector<std::string> macro_blocks_;
  std::vector<int64_t>     macro_seqs_;
  std::string             data_file_path_;
  std::string             meta_file_path_;
  ObSSTableMeta           meta_;
  ObSSTable               sstable_;
  bool                    is_closed_;
};

}  // namespace blocksstable
}  // namespace oceanbase
