/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved. miniob is licensed under Mulan PSL v2. */

#include "storage/ls/ob_memtable_mgr.h"

namespace oceanbase {
namespace storage {

ObMemTableMgr::ObMemTableMgr() : tablet_id_(0), active_memtable_(nullptr), is_inited_(false) {}

ObMemTableMgr::~ObMemTableMgr()
{
  delete active_memtable_;
  for (auto *mt : frozen_memtables_) delete mt;
}

int ObMemTableMgr::init(int64_t tablet_id)
{
  tablet_id_ = tablet_id;
  is_inited_ = true;
  return 0;
}

memtable::ObMemtable *ObMemTableMgr::get_active_memtable()
{
  std::lock_guard<std::mutex> guard(mutex_);
  if (active_memtable_ == nullptr) {
    active_memtable_ = new memtable::ObMemtable();
    active_memtable_->init(tablet_id_);
  }
  return active_memtable_;
}

int ObMemTableMgr::set_is_tablet_freeze()
{
  std::lock_guard<std::mutex> guard(mutex_);
  if (active_memtable_ == nullptr) return 0;
  active_memtable_->set_frozen();
  frozen_memtables_.push_back(active_memtable_);
  active_memtable_ = nullptr;
  get_active_memtable();  // create new active
  return 0;
}

const std::vector<memtable::ObMemtable *> &ObMemTableMgr::get_frozen_memtables() const
{
  return frozen_memtables_;
}

void ObMemTableMgr::get_all_memtables(std::vector<memtable::ObMemtable *> &out) const
{
  std::lock_guard<std::mutex> guard(mutex_);
  if (active_memtable_) out.push_back(active_memtable_);
  for (auto *mt : frozen_memtables_) out.push_back(mt);
}

int64_t ObMemTableMgr::get_total_occupied_size() const
{
  std::lock_guard<std::mutex> guard(mutex_);
  int64_t total = 0;
  if (active_memtable_) total += active_memtable_->get_occupy_size();
  for (auto *mt : frozen_memtables_) total += mt->get_occupy_size();
  return total;
}

int ObMemTableMgr::remove_frozen_memtable(memtable::ObMemtable *mt)
{
  std::lock_guard<std::mutex> guard(mutex_);
  for (auto it = frozen_memtables_.begin(); it != frozen_memtables_.end(); ++it) {
    if (*it == mt) {
      delete *it;
      frozen_memtables_.erase(it);
      return 0;
    }
  }
  return -1;
}

}  // namespace storage
}  // namespace oceanbase
