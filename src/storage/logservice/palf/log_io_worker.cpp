/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/log_io_worker.h */

#include "log_io_worker.h"

namespace oceanbase {
namespace palf {

LogIOWorker::LogIOWorker() : stop_(false), is_inited_(false) {}

LogIOWorker::~LogIOWorker() { destroy(); }

int LogIOWorker::init()
{
  if (is_inited_) return -1;
  stop_ = false;
  thread_ = std::thread(&LogIOWorker::run_, this);
  is_inited_ = true;
  return 0;
}

void LogIOWorker::destroy()
{
  if (!is_inited_) return;
  stop_ = true;
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
  is_inited_ = false;
}

int LogIOWorker::submit_io_task(IOTask task)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    task_queue_.push(std::move(task));
  }
  cv_.notify_one();
  return 0;
}

void LogIOWorker::run_()
{
  while (!stop_) {
    IOTask task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return !task_queue_.empty() || stop_; });
      if (stop_ && task_queue_.empty()) break;
      if (!task_queue_.empty()) {
        task = std::move(task_queue_.front());
        task_queue_.pop();
      }
    }
    if (task) task();
  }
}

}  // namespace palf
}  // namespace oceanbase
