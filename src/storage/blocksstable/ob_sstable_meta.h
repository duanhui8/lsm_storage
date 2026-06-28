/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/blockblocksstable/ob_sstable_meta.h */

#pragma once

#include <string>
#include <vector>

#include "storage/ob_define.h"

namespace oceanbase {
namespace blocksstable {

/**
 * ObSSTableBasicMeta — core metadata of an SSTable.
 * Simplified from OB 4.4.2 ob_sstable_meta.h.
 */
struct ObSSTableBasicMeta
{
  int64_t row_count_;
  int64_t occupy_size_;
  int64_t original_size_;
  int64_t data_checksum_;
  int64_t rowkey_column_count_;
  int64_t column_cnt_;
  int64_t data_macro_block_count_;
  int64_t data_micro_block_count_;
  int64_t schema_version_;
  int64_t create_snapshot_version_;
  int64_t upper_trans_version_;
  int64_t max_merged_trans_version_;
  int16_t data_index_tree_height_;    // =1 for single-level index
  uint8_t row_store_type_;
  bool    contain_uncommitted_row_;

  ObSSTableBasicMeta() { std::memset(this, 0, sizeof(*this)); }
};

/**
 * ObRootBlockInfo — root of the index tree.
 * Simplified: stores a single root block as inline data.
 */
struct ObRootBlockInfo
{
  std::string root_block_data_;   // serialized root index block
  int64_t     root_block_offset_;
  int64_t     root_block_size_;

  ObRootBlockInfo() : root_block_offset_(0), root_block_size_(0) {}
};

/**
 * ObSSTableMacroInfo — list of macro block IDs in this SSTable.
 */
struct ObSSTableMacroInfo
{
  std::vector<int64_t> macro_ids_;       // macro block sequence numbers
  std::vector<int64_t> macro_offsets_;   // file offsets for each macro block
  std::vector<int64_t> macro_sizes_;     // sizes of each macro block
};

/**
 * ObSSTableMeta — full SSTable metadata.
 * Simplified from OB 4.4.2 ob_sstable_meta.h.
 */
class ObSSTableMeta
{
public:
  ObSSTableMeta();
  ~ObSSTableMeta();

  int init(int64_t                       tablet_id,
           int64_t                       column_count,
           int64_t                       rowkey_column_count,
           int64_t                       schema_version,
           int64_t                       snapshot_version);

  int add_macro_block(int64_t macro_seq, int64_t macro_offset, int64_t macro_size,
                       int64_t row_count, int64_t micro_block_count);

  int set_root_index_block(const char *data, int64_t size);

  // Accessors
  const ObSSTableBasicMeta &get_basic_meta() const { return basic_meta_; }
  int64_t get_row_count() const { return basic_meta_.row_count_; }
  int64_t get_occupy_size() const { return basic_meta_.occupy_size_; }
  int64_t get_macro_block_count() const { return basic_meta_.data_macro_block_count_; }
  int64_t get_micro_block_count() const { return basic_meta_.data_micro_block_count_; }

  // Serialization to/from file (simple binary format)
  int serialize(std::string &out) const;
  int deserialize(const std::string &in);
  int save_to_file(const std::string &path) const;
  int load_from_file(const std::string &path);

  ObSSTableBasicMeta basic_meta_;
  ObRootBlockInfo     data_root_info_;
  ObSSTableMacroInfo  macro_info_;

private:
  bool is_inited_;
};

}  // namespace blocksstable
}  // namespace oceanbase
