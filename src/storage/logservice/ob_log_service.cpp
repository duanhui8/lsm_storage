/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/ob_log_service.cpp */

#include "ob_log_service.h"
#include "palf/log_entry.h"
#include <cstdlib>

namespace oceanbase {
namespace logservice {

ObLogService::ObLogService() : is_inited_(false), is_running_(false) {}
ObLogService::~ObLogService() { destroy(); }

int ObLogService::init(const char *base_dir)
{
  if (is_inited_) return -1;
  base_dir_ = base_dir;

  int ret = palf_env_impl_.init(base_dir);
  if (ret != 0) return ret;

  is_inited_ = true;
  return 0;
}

int ObLogService::start()
{
  if (!is_inited_ || is_running_) return -1;
  int ret = palf_env_impl_.start();
  if (ret != 0) return ret;
  is_running_ = true;
  return 0;
}

void ObLogService::stop()
{
  is_running_ = false;
  palf_env_impl_.stop();
}

void ObLogService::destroy()
{
  stop();
  for (auto &pair : handlers_) {
    delete pair.second;
  }
  handlers_.clear();
  palf_env_impl_.destroy();
  is_inited_ = false;
}

int ObLogService::open_ls(int64_t ls_id, ObLogHandler *&handler)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handlers_.find(ls_id);
    if (it != handlers_.end()) {
      handler = it->second;
      return 0;
    }
  }

  ObLogHandler *h = new ObLogHandler();
  int ret = h->init(this, ls_id);
  if (ret != 0) {
    delete h;
    return ret;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[ls_id] = h;
  }
  handler = h;
  return 0;
}

int ObLogService::remove_ls(int64_t ls_id)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = handlers_.find(ls_id);
  if (it == handlers_.end()) return -1;
  delete it->second;
  handlers_.erase(it);
  palf_env_impl_.remove_palf_handle_impl(ls_id);
  return 0;
}

int ObLogService::replay_ls(int64_t ls_id)
{
  ObLogHandler *handler = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = handlers_.find(ls_id);
    if (it == handlers_.end()) return -1;
    handler = it->second;
  }

  palf::PalfHandle *handle = handler->get_palf_handle();
  palf::LSN begin_lsn, end_lsn;
  int ret = handle->get_begin_lsn(begin_lsn);
  if (ret != 0) return ret;
  ret = handle->get_end_lsn(end_lsn);
  if (ret != 0) return ret;

  // Walk log entries from begin to end
  palf::LSN current = begin_lsn;
  char read_buf[4096];

  while (current.val_ < end_lsn.val_) {
    palf::ReadBuf rbuf(read_buf, sizeof(read_buf));
    int64_t out_size = 0;
    ret = handle->read(current, sizeof(read_buf), rbuf, out_size);
    if (ret != 0) break;
    if (out_size == 0) break;

    // Parse LogGroupEntryHeader
    if (out_size < palf::LogGroupEntryHeader::get_serialize_size()) break;

    palf::LogGroupEntryHeader group_header;
    int64_t pos = 0;
    ret = group_header.deserialize(read_buf, out_size, pos);
    if (ret != 0) break;
    if (!group_header.is_valid()) break;

    // Replay the payload (after group entry header)
    const char *payload = read_buf + pos;
    int64_t payload_len = group_header.group_entry_size_ - pos;
    if (payload_len > 0) {
      handler->replay(payload, payload_len, current, 0);
    }

    // Advance
    current = group_header.committed_end_lsn_;
    if (current.val_ <= begin_lsn.val_) break; // prevent infinite loop
  }

  return 0;
}

}  // namespace logservice
}  // namespace oceanbase
