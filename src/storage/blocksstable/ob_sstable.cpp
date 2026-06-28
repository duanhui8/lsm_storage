/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include <fstream>

#include "storage/blocksstable/ob_sstable.h"

namespace oceanbase {
namespace blocksstable {

ObSSTable::ObSSTable() : is_inited_(false) {}

ObSSTable::~ObSSTable() = default;

int ObSSTable::init(const std::string &data_file_path,
                     const std::string &meta_file_path)
{
  data_file_path_ = data_file_path;
  meta_file_path_ = meta_file_path;

  int ret = meta_.load_from_file(meta_file_path);
  if (ret != 0) return ret;

  is_inited_ = true;
  return 0;
}

int ObSSTable::create_from_blocks(const std::vector<std::string> &macro_blocks,
                                   const std::vector<int64_t>     &macro_seqs,
                                   const ObSSTableMeta            &meta,
                                   const std::string              &data_file_path,
                                   const std::string              &meta_file_path)
{
  meta_            = meta;
  data_file_path_  = data_file_path;
  meta_file_path_  = meta_file_path;

  // Write all macro blocks to data file
  std::ofstream ofs(data_file_path, std::ios::binary);
  if (!ofs.is_open()) return -1;

  ObSSTableMeta write_meta = meta;
  write_meta.macro_info_.macro_ids_.clear();
  write_meta.macro_info_.macro_offsets_.clear();
  write_meta.macro_info_.macro_sizes_.clear();

  int64_t offset = 0;
  for (size_t i = 0; i < macro_blocks.size(); i++) {
    ofs.write(macro_blocks[i].data(), macro_blocks[i].size());
    write_meta.add_macro_block(macro_seqs[i], offset, macro_blocks[i].size(),
                                0, 0);  // row/micro counts in macro header
    offset += macro_blocks[i].size();
  }
  ofs.close();

  // Save meta
  write_meta.save_to_file(meta_file_path_);

  meta_    = write_meta;
  is_inited_ = true;
  return 0;
}

int ObSSTable::read_macro_block(int64_t macro_idx, std::string &out_data) const
{
  if (!is_inited_) return -1;
  if (macro_idx < 0 || macro_idx >= static_cast<int64_t>(meta_.macro_info_.macro_offsets_.size())) {
    return -1;
  }

  int64_t offset = meta_.macro_info_.macro_offsets_[macro_idx];
  int64_t size   = meta_.macro_info_.macro_sizes_[macro_idx];

  std::ifstream ifs(data_file_path_, std::ios::binary);
  if (!ifs.is_open()) return -1;

  out_data.resize(size);
  ifs.seekg(offset);
  ifs.read(&out_data[0], size);
  ifs.close();
  return 0;
}

}  // namespace blocksstable
}  // namespace oceanbase
