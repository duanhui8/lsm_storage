/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved. miniob is licensed under Mulan PSL v2. */

#include "storage/ls/ob_freezer.h"

namespace oceanbase {
namespace storage {

ObFreezer::ObFreezer() : mgr_(nullptr), threshold_(0), is_inited_(false) {}
ObFreezer::~ObFreezer() = default;

int ObFreezer::init(ObMemTableMgr *mgr, int64_t memtable_size_threshold)
{
  mgr_       = mgr;
  threshold_ = memtable_size_threshold;
  is_inited_ = true;
  return 0;
}

bool ObFreezer::try_freeze()
{
  if (!is_inited_ || OB_ISNULL(mgr_)) return false;
  int64_t total = mgr_->get_total_occupied_size();
  if (total >= threshold_) {
    mgr_->set_is_tablet_freeze();
    return true;
  }
  return false;
}

}  // namespace storage
}  // namespace oceanbase
