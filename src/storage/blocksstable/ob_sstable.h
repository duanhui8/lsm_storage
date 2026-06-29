/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/blockblocksstable/ob_sstable.h */

/* ========== ObSSTable — 磁盘有序字符串表(对应 OB 4.4.2 storage/blocksstable/ob_sstable.h)
 * OB 4.4.2: LSM-Tree 的磁盘层,一个 SSTable 由多个 Macro Block(2MB)组成,
 *          每个Macro Block内包含多个 Micro Block(压缩的行数据)+ B+Tree索引。
 * MiniOB: 简化版,通过 block_file 存储,支持 read_macro_block()。
 * ========== */
#pragma once

#include <string>
#include <vector>
#include <memory>

#include "storage/ob_i_table.h"
#include "storage/blocksstable/ob_sstable_meta.h"

namespace oceanbase {
namespace blocksstable {

/**
 * ObSSTable — represents an on-disk sorted string table.
 * Simplified from OB 4.4.2 ob_sstable.h.
 *
 * Inherits ObITable for uniform access with memtables.
 * Manages data file + meta file on disk.
 */
class ObSSTable : public storage::ObITable
{
public:
  ObSSTable();
  virtual ~ObSSTable();

  /** Init an existing SSTable from disk files. */
  int init(const std::string &data_file_path,
           const std::string &meta_file_path);

  /** Create a new SSTable from built data. */
  int create_from_blocks(const std::vector<std::string> &macro_blocks,
                          const std::vector<int64_t>     &macro_seqs,
                          const ObSSTableMeta            &meta,
                          const std::string              &data_file_path,
                          const std::string              &meta_file_path);

  // ---- ObITable interface ----
  storage::ObTableType get_table_type() const override { return storage::ObTableType::MINI_SSTABLE; }
  int64_t get_row_count() const override { return meta_.get_row_count(); }
  int64_t get_occupy_size() const override { return meta_.get_occupy_size(); }
  bool    is_empty() const override { return meta_.get_row_count() == 0; }

  /** Get the meta. */
  const ObSSTableMeta &get_meta() const { return meta_; }

  /** Read a macro block by index. */
  int read_macro_block(int64_t macro_idx, std::string &out_data) const;

  /** Get file path. */
  const std::string &get_data_file_path() const { return data_file_path_; }

private:
  ObSSTableMeta  meta_;
  std::string    data_file_path_;
  std::string    meta_file_path_;
  bool           is_inited_;
};

}  // namespace blocksstable
}  // namespace oceanbase
