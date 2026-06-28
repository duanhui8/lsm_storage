/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/log_io_worker.h */

#ifndef OCEANBASE_LOGSERVICE_LOG_IO_WORKER_
#define OCEANBASE_LOGSERVICE_LOG_IO_WORKER_
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include "log_io_context.h"

namespace oceanbase {
namespace palf {

/**
 * LogIOWorker — background thread for async log I/O.
 * Simplified from OB 4.4.2 log_io_worker.h.
 *
 * For single-node mystorage: a single background thread that processes
 * queued flush tasks. Each task is a function that writes to LogStorage.
 */
class LogIOWorker {
public:
  using IOTask = std::function<int()>;

  LogIOWorker();
  ~LogIOWorker();

  int init();
  void destroy();

  int submit_io_task(IOTask task);

private:
  void run_();

  std::thread              thread_;
  std::queue<IOTask>       task_queue_;
  std::mutex               mutex_;
  std::condition_variable  cv_;
  std::atomic<bool>        stop_;
  bool                     is_inited_;
};

}  // namespace palf
}  // namespace oceanbase
#endif
