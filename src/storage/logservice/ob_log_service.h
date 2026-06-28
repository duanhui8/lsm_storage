/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/ob_log_service.h */

#ifndef OCEANBASE_LOGSERVICE_OB_LOG_SERVICE_
#define OCEANBASE_LOGSERVICE_OB_LOG_SERVICE_

#include "ob_log_handler.h"
#include "palf/palf_env_impl.h"
#include <unordered_map>
#include <mutex>
#include <string>

namespace oceanbase {
namespace logservice {

/**
 * ObLogService — tenant-level log service.
 * Simplified from OB 4.4.2 ob_log_service.h:40-313.
 *
 * Manages:
 *  - PalfEnvImpl (the PALF environment)
 *  - All ObLogHandlers (one per LS/tablet)
 *  - Startup and recovery coordination
 */
class ObLogService {
public:
  ObLogService();
  ~ObLogService();

  int init(const char *base_dir);
  int start();
  void stop();
  void destroy();

  // === LS lifecycle ===
  int open_ls(int64_t ls_id, ObLogHandler *&handler);
  int remove_ls(int64_t ls_id);

  // === Recovery ===
  // Replay all committed logs for a given LS, calling registered handlers.
  int replay_ls(int64_t ls_id);

  palf::PalfEnvImpl *get_palf_env() { return &palf_env_impl_; }

  const std::string &get_base_dir() const { return base_dir_; }

private:
  std::string        base_dir_;
  palf::PalfEnvImpl  palf_env_impl_;
  std::unordered_map<int64_t, ObLogHandler *> handlers_;
  std::mutex         mutex_;
  bool               is_inited_;
  bool               is_running_;
};

}  // namespace logservice
}  // namespace oceanbase

#endif
