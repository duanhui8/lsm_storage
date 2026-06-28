/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/palf_env_impl.h */

#include "palf_env_impl.h"
#include <cstdio>

namespace oceanbase {
namespace palf {

PalfEnvImpl::PalfEnvImpl() : is_inited_(false), is_running_(false) {}
PalfEnvImpl::~PalfEnvImpl() { destroy(); }

int PalfEnvImpl::init(const char *base_dir)
{
  if (is_inited_) return -1;
  base_dir_ = base_dir;
  log_dir_ = base_dir_ + "/logdata";
  std::string cmd = "mkdir -p " + log_dir_;
  ::system(cmd.c_str());

  int ret = io_worker_.init();
  if (ret != 0) return ret;

  is_inited_ = true;
  return 0;
}

int PalfEnvImpl::start()
{
  if (!is_inited_ || is_running_) return -1;
  // Start background IO worker
  is_running_ = true;
  return 0;
}

void PalfEnvImpl::stop()
{
  is_running_ = false;
}

void PalfEnvImpl::destroy()
{
  stop();
  for (auto &pair : handles_) {
    delete pair.second;
  }
  handles_.clear();
  io_worker_.destroy();
  is_inited_ = false;
}

int PalfEnvImpl::create_palf_handle_impl(int64_t palf_id,
                                          AccessMode,
                                          IPalfHandleImpl *&impl)
{
  std::string ls_log_dir = log_dir_ + "/" + std::to_string(palf_id);
  std::string cmd = "mkdir -p " + ls_log_dir;
  ::system(cmd.c_str());

  PalfHandleImpl *handle = new PalfHandleImpl();
  int ret = handle->init(palf_id, ls_log_dir.c_str(), &io_worker_);
  if (ret != 0) {
    delete handle;
    return ret;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    handles_[palf_id] = handle;
  }
  impl = handle;
  return 0;
}

int PalfEnvImpl::get_palf_handle_impl(int64_t palf_id, IPalfHandleImpl *&impl)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = handles_.find(palf_id);
  if (it == handles_.end()) return -1;
  impl = it->second;
  return 0;
}

int PalfEnvImpl::remove_palf_handle_impl(int64_t palf_id)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = handles_.find(palf_id);
  if (it == handles_.end()) return -1;
  delete it->second;
  handles_.erase(it);
  return 0;
}

int PalfEnvImpl::reload_palf_handle_impl(int64_t palf_id)
{
  std::string ls_log_dir = log_dir_ + "/" + std::to_string(palf_id);
  PalfHandleImpl *handle = new PalfHandleImpl();
  int ret = handle->load(palf_id, ls_log_dir.c_str(), &io_worker_);
  if (ret != 0) {
    delete handle;
    return ret;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    handles_[palf_id] = handle;
  }
  return 0;
}

}  // namespace palf
}  // namespace oceanbase
