/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved. miniob is licensed under Mulan PSL v2. */

#include "storage/tablet/ob_tablet.h"
#include "storage/logservice/ob_log_service.h"

namespace oceanbase {
namespace storage {

ObTablet::ObTablet() : tablet_id_(0), log_handler_(nullptr), log_service_(nullptr), is_inited_(false) {}
ObTablet::~ObTablet()
{
  if (log_handler_ != nullptr && log_service_ != nullptr) {
    log_service_->remove_ls(tablet_id_);
    log_handler_ = nullptr;
  }
}

int ObTablet::init(int64_t tablet_id, logservice::ObLogService *log_service)
{
  tablet_id_ = tablet_id;
  table_store_.init(tablet_id);
  freezer_.init(table_store_.get_memtable_mgr(), blocksstable::DEFAULT_MEMTABLE_SIZE);

  // Init log handler if log service is available
  if (log_service != nullptr) {
    log_service_ = log_service;
    int ret = log_service->open_ls(tablet_id, log_handler_);
    if (ret != 0) return ret;
    table_store_.set_log_handler(log_handler_);
  }

  is_inited_ = true;
  return 0;
}

int ObTablet::recover()
{
  if (log_service_ == nullptr) return 0;
  return log_service_->replay_ls(tablet_id_);
}

}  // namespace storage
}  // namespace oceanbase
