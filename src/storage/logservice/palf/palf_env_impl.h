/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/palf_env_impl.h */

#ifndef OCEANBASE_LOGSERVICE_PALF_ENV_IMPL_
#define OCEANBASE_LOGSERVICE_PALF_ENV_IMPL_
#include "palf_handle_impl.h"
#include "log_io_worker.h"
#include <unordered_map>
#include <string>
#include <mutex>

namespace oceanbase {
namespace palf {

/**
 * PalfEnvImpl — per-tenant container of all PalfHandleImpl instances.
 * Simplified from OB 4.4.2 palf_env_impl.h:227-441.
 *
 * Manages:
 *  - LogIOWorker (shared across all handles)
 *  - Map of ls_id → PalfHandleImpl*
 */
class PalfEnvImpl {
public:
  PalfEnvImpl();
  ~PalfEnvImpl();

  int init(const char *base_dir);
  int start();
  void stop();
  void destroy();

  // === Handle lifecycle ===
  int create_palf_handle_impl(int64_t palf_id,
                               AccessMode access_mode,
                               IPalfHandleImpl *&impl);
  int get_palf_handle_impl(int64_t palf_id, IPalfHandleImpl *&impl);
  int remove_palf_handle_impl(int64_t palf_id);

  int reload_palf_handle_impl(int64_t palf_id);

  LogIOWorker *get_io_worker() { return &io_worker_; }
  const std::string &get_log_dir() const { return log_dir_; }

private:
  std::string        base_dir_;
  std::string        log_dir_;
  LogIOWorker        io_worker_;
  std::mutex         mutex_;
  std::unordered_map<int64_t, IPalfHandleImpl *> handles_;
  bool               is_inited_;
  bool               is_running_;
};

}  // namespace palf
}  // namespace oceanbase
#endif
