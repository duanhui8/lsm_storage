/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/ob_log_handler.h */

#ifndef OCEANBASE_LOGSERVICE_OB_LOG_HANDLER_
#define OCEANBASE_LOGSERVICE_OB_LOG_HANDLER_

#include "ob_log_base_type.h"
#include "ob_log_base_header.h"
#include "palf/palf_handle.h"
#include "palf/palf_handle_impl.h"
#include <unordered_map>
#include <functional>
#include <mutex>
#include <memory>

namespace oceanbase {
namespace logservice {

class ObLogService;

/**
 * ObIReplaySubHandler — interface for modules that need to replay CLOG.
 * Each ObLogBaseType registers one handler.
 */
class ObIReplaySubHandler {
public:
  virtual ~ObIReplaySubHandler() = default;
  virtual int replay(const void *buffer, int64_t nbytes,
                     const palf::LSN &lsn, int64_t scn) = 0;
};

/**
 * ObIRoleChangeSubHandler — notified when leader/follower role changes.
 */
class ObIRoleChangeSubHandler {
public:
  virtual ~ObIRoleChangeSubHandler() = default;
  virtual int switch_to_leader() = 0;
  virtual int switch_to_follower_gracefully() = 0;
  virtual int resume_leader() = 0;
};

/**
 * ObLogHandler — per-LS CLOG handler.
 * Simplified from OB 4.4.2 ob_log_handler.h:180-1131.
 *
 * Each tablet/LS has one ObLogHandler. It:
 *  - Owns a PalfHandle (log stream handle)
 *  - Manages registered replay handlers
 *  - Manages registered role change handlers
 *  - Provides append() to write to CLOG
 *  - Provides replay() to replay persisted logs
 */
class ObLogHandler {
public:
  ObLogHandler();
  ~ObLogHandler();

  int init(ObLogService *log_service, int64_t ls_id);

  // === Registration ===
  int register_replay_handler(ObLogBaseType type, ObIReplaySubHandler *handler);
  int register_role_change_handler(ObLogBaseType type, ObIRoleChangeSubHandler *handler);

  // === Write path ===
  // Append a log entry into CLOG. Returns the assigned LSN.
  int append(const void *buf, int64_t buf_len, ObLogBaseType type,
             palf::LSN &lsn, int64_t &scn);

  // === Read/Replay path ===
  int replay(const void *buf, int64_t buf_len, const palf::LSN &lsn, int64_t scn);

  // === Access ===
  palf::PalfHandle *get_palf_handle() { return &palf_handle_; }
  int64_t get_ls_id() const { return ls_id_; }

  // === Role change ===
  int switch_to_leader();
  int switch_to_follower();

private:
  int64_t         ls_id_;
  palf::PalfHandle palf_handle_;
  palf::IPalfHandleImpl *palf_handle_impl_;

  std::unordered_map<int16_t, ObIReplaySubHandler *>   replay_handlers_;
  std::unordered_map<int16_t, ObIRoleChangeSubHandler *> role_change_handlers_;

  std::mutex mutex_;
  bool is_inited_;
};

}  // namespace logservice
}  // namespace oceanbase

#endif
