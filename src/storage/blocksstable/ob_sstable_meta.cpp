/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include <fstream>
#include <cstring>
#include <cstdio>

#include "storage/blocksstable/ob_sstable_meta.h"

namespace oceanbase {
namespace blocksstable {

ObSSTableMeta::ObSSTableMeta() : is_inited_(false) {}

ObSSTableMeta::~ObSSTableMeta() = default;

int ObSSTableMeta::init(int64_t tablet_id,
                         int64_t column_count,
                         int64_t rowkey_column_count,
                         int64_t schema_version,
                         int64_t snapshot_version)
{
  basic_meta_.rowkey_column_count_      = rowkey_column_count;
  basic_meta_.column_cnt_               = column_count;
  basic_meta_.schema_version_           = schema_version;
  basic_meta_.create_snapshot_version_  = snapshot_version;
  basic_meta_.data_index_tree_height_   = 1;  // single-level
  basic_meta_.row_store_type_           = 0;  // FLAT
  basic_meta_.contain_uncommitted_row_  = false;
  is_inited_ = true;
  return 0;
}

int ObSSTableMeta::add_macro_block(int64_t macro_seq, int64_t macro_offset,
                                    int64_t macro_size, int64_t row_count,
                                    int64_t micro_block_count)
{
  macro_info_.macro_ids_.push_back(macro_seq);
  macro_info_.macro_offsets_.push_back(macro_offset);
  macro_info_.macro_sizes_.push_back(macro_size);
  basic_meta_.data_macro_block_count_++;
  basic_meta_.data_micro_block_count_ += micro_block_count;
  basic_meta_.row_count_ += row_count;
  basic_meta_.occupy_size_ += macro_size;
  return 0;
}

int ObSSTableMeta::set_root_index_block(const char *data, int64_t size)
{
  data_root_info_.root_block_data_.assign(data, size);
  data_root_info_.root_block_size_ = size;
  return 0;
}

int ObSSTableMeta::serialize(std::string &out) const
{
  // Simple binary format: basic_meta + root_block_len + root_block + macro_info arrays
  out.append(reinterpret_cast<const char *>(&basic_meta_), sizeof(basic_meta_));
  int64_t root_size = data_root_info_.root_block_data_.size();
  out.append(reinterpret_cast<const char *>(&root_size), sizeof(root_size));
  if (root_size > 0) {
    out.append(data_root_info_.root_block_data_);
  }
  int64_t macro_count = static_cast<int64_t>(macro_info_.macro_ids_.size());
  out.append(reinterpret_cast<const char *>(&macro_count), sizeof(macro_count));
  for (int64_t i = 0; i < macro_count; i++) {
    out.append(reinterpret_cast<const char *>(&macro_info_.macro_ids_[i]), sizeof(int64_t));
    out.append(reinterpret_cast<const char *>(&macro_info_.macro_offsets_[i]), sizeof(int64_t));
    out.append(reinterpret_cast<const char *>(&macro_info_.macro_sizes_[i]), sizeof(int64_t));
  }
  return 0;
}

int ObSSTableMeta::deserialize(const std::string &in)
{
  const char *ptr = in.data();
  int64_t remaining = in.size();

  if (remaining < static_cast<int64_t>(sizeof(basic_meta_))) return -1;
  std::memcpy(&basic_meta_, ptr, sizeof(basic_meta_));
  ptr += sizeof(basic_meta_); remaining -= sizeof(basic_meta_);

  int64_t root_size = 0;
  if (remaining < static_cast<int64_t>(sizeof(root_size))) return -1;
  std::memcpy(&root_size, ptr, sizeof(root_size));
  ptr += sizeof(root_size); remaining -= sizeof(root_size);

  if (root_size > 0 && remaining >= root_size) {
    data_root_info_.root_block_data_.assign(ptr, root_size);
    ptr += root_size; remaining -= root_size;
  }

  int64_t macro_count = 0;
  if (remaining < static_cast<int64_t>(sizeof(macro_count))) return -1;
  std::memcpy(&macro_count, ptr, sizeof(macro_count));
  ptr += sizeof(macro_count); remaining -= sizeof(macro_count);

  for (int64_t i = 0; i < macro_count && remaining >= 24; i++) {
    int64_t id, off, sz;
    std::memcpy(&id, ptr, 8); ptr += 8;
    std::memcpy(&off, ptr, 8); ptr += 8;
    std::memcpy(&sz, ptr, 8); ptr += 8;
    remaining -= 24;
    macro_info_.macro_ids_.push_back(id);
    macro_info_.macro_offsets_.push_back(off);
    macro_info_.macro_sizes_.push_back(sz);
  }
  basic_meta_.data_macro_block_count_ = macro_count;
  return 0;
}

int ObSSTableMeta::save_to_file(const std::string &path) const
{
  std::ofstream ofs(path, std::ios::binary);
  if (!ofs.is_open()) return -1;
  std::string data;
  serialize(data);
  ofs.write(data.data(), data.size());
  ofs.close();
  return 0;
}

int ObSSTableMeta::load_from_file(const std::string &path)
{
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) return -1;
  ifs.seekg(0, std::ios::end);
  int64_t sz = ifs.tellg();
  ifs.seekg(0, std::ios::beg);
  std::string data(sz, '\0');
  ifs.read(&data[0], sz);
  ifs.close();
  return deserialize(data);
}

}  // namespace blocksstable
}  // namespace oceanbase
