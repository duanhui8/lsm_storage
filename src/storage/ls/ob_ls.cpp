/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved. miniob is licensed under Mulan PSL v2. */

#include <filesystem>

#include "storage/ls/ob_ls.h"

namespace oceanbase {
namespace storage {

ObStore::ObStore() : is_inited_(false) {}
ObStore::~ObStore()
{
  for (auto &pair : tablets_by_name_) delete pair.second;
  tablets_by_name_.clear();
  tablets_by_id_.clear();
}

int ObStore::init(const std::string &base_dir)
{
  base_dir_ = base_dir;
  if (!std::filesystem::is_directory(base_dir_)) {
    std::filesystem::create_directories(base_dir_);
  }
  is_inited_ = true;
  return 0;
}

int ObStore::create_tablet(const std::string &table_name, int64_t tablet_id)
{
  std::lock_guard<std::mutex> guard(mutex_);
  if (tablets_by_name_.find(table_name) != tablets_by_name_.end()) {
    return -1;  // Already exists
  }
  ObTablet *tablet = new ObTablet();
  tablet->init(tablet_id);
  tablets_by_name_[table_name] = tablet;
  tablets_by_id_[tablet_id]    = tablet;
  return 0;
}

ObTablet *ObStore::get_tablet(const std::string &table_name)
{
  std::lock_guard<std::mutex> guard(mutex_);
  auto it = tablets_by_name_.find(table_name);
  return (it != tablets_by_name_.end()) ? it->second : nullptr;
}

ObTablet *ObStore::get_tablet(int64_t tablet_id)
{
  std::lock_guard<std::mutex> guard(mutex_);
  auto it = tablets_by_id_.find(tablet_id);
  return (it != tablets_by_id_.end()) ? it->second : nullptr;
}

int ObStore::drop_tablet(const std::string &table_name)
{
  std::lock_guard<std::mutex> guard(mutex_);
  auto it = tablets_by_name_.find(table_name);
  if (it == tablets_by_name_.end()) return -1;
  ObTablet *t = it->second;
  tablets_by_name_.erase(it);
  tablets_by_id_.erase(t->get_tablet_id());
  delete t;
  return 0;
}

void ObStore::try_freeze_all()
{
  std::lock_guard<std::mutex> guard(mutex_);
  for (auto &pair : tablets_by_name_) {
    pair.second->get_freezer()->try_freeze();
  }
}

}  // namespace storage
}  // namespace oceanbase
