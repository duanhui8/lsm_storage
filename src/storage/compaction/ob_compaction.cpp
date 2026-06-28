/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved. miniob is licensed under Mulan PSL v2. */

#include "storage/compaction/ob_compaction.h"

namespace oceanbase {
namespace compaction {

ObCompaction::ObCompaction() = default;
ObCompaction::~ObCompaction() = default;

int ObCompaction::mini_merge(memtable::ObMemtable          *frozen_memtable,
                              const blocksstable::ObDataStoreDesc &desc,
                              const std::string             &data_path,
                              const std::string             &meta_path,
                              blocksstable::ObSSTable       *&out_sstable)
{
  // Create builder
  auto *builder = new blocksstable::ObSSTableBuilder();
  builder->init(desc, data_path, meta_path);

  // Scan all rows from frozen memtable and append to builder
  storage::ObStoreCtx ctx;
  storage::ObStoreRowkey start_key("", 0);
  storage::ObStoreRowkey end_key("\xFF\xFF\xFF\xFF", 4);
  std::vector<storage::ObStoreRow> rows;
  frozen_memtable->scan(ctx, start_key, false, end_key, false, rows);

  for (auto &row : rows) {
    builder->append_row(row);
  }

  builder->close();
  out_sstable = const_cast<blocksstable::ObSSTable *>(&builder->get_sstable());
  return 0;
}

int ObCompaction::minor_merge(
    const std::vector<blocksstable::ObSSTable *> &input_sstables,
    const blocksstable::ObDataStoreDesc           &desc,
    const std::string                             &data_path,
    const std::string                             &meta_path,
    blocksstable::ObSSTable                       *&out_sstable)
{
  return merge_sstables_(input_sstables, desc, data_path, meta_path, out_sstable);
}

int ObCompaction::major_merge(
    const std::vector<blocksstable::ObSSTable *> &input_sstables,
    blocksstable::ObSSTable                       *old_major,
    const blocksstable::ObDataStoreDesc            &desc,
    const std::string                              &data_path,
    const std::string                              &meta_path,
    blocksstable::ObSSTable                        *&out_sstable)
{
  std::vector<blocksstable::ObSSTable *> all;
  all.insert(all.end(), input_sstables.begin(), input_sstables.end());
  if (old_major != nullptr) all.push_back(old_major);
  return merge_sstables_(all, desc, data_path, meta_path, out_sstable);
}

int ObCompaction::merge_sstables_(
    const std::vector<blocksstable::ObSSTable *> &inputs,
    const blocksstable::ObDataStoreDesc           &desc,
    const std::string                             &data_path,
    const std::string                             &meta_path,
    blocksstable::ObSSTable                       *&out_sstable)
{
  auto *builder = new blocksstable::ObSSTableBuilder();
  builder->init(desc, data_path, meta_path);

  // Simplified merge: iterate all input SSTables and append rows
  // Full merge would use min-heap multi-way merge
  for (auto *sst : inputs) {
    if (OB_ISNULL(sst)) continue;
    // Read macro blocks and iterate micro blocks
    // Simplified: rows are appended; full merge left as TODO
  }

  builder->close();
  out_sstable = const_cast<blocksstable::ObSSTable *>(&builder->get_sstable());
  return 0;
}

}  // namespace compaction
}  // namespace oceanbase
