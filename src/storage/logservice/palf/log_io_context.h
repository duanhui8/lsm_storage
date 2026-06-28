/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/log_io_context.h */

#ifndef OCEANBASE_LOGSERVICE_LOG_IO_CONTEXT_
#define OCEANBASE_LOGSERVICE_LOG_IO_CONTEXT_
#include <cstdint>
#include "lsn.h"

namespace oceanbase {
namespace palf {

// Callback context for flush log completion
struct FlushLogCbCtx {
  int64_t palf_id_;
  int64_t log_id_;
  LSN     lsn_;
  int64_t log_proposal_id_;
  FlushLogCbCtx() : palf_id_(INVALID_PALF_ID), log_id_(0), log_proposal_id_(0) {}
};

// Callback context for flush meta completion
struct FlushMetaCbCtx {
  int64_t palf_id_;
  FlushMetaCbCtx() : palf_id_(INVALID_PALF_ID) {}
};

// Callback context for truncate log
struct TruncateLogCbCtx {
  int64_t palf_id_;
  LSN     lsn_;
  TruncateLogCbCtx() : palf_id_(INVALID_PALF_ID) {}
};

// Callback context for truncate prefix blocks
struct TruncatePrefixBlocksCbCtx {
  int64_t palf_id_;
  LSN     lsn_;
  TruncatePrefixBlocksCbCtx() : palf_id_(INVALID_PALF_ID) {}
};

// I/O timeout
constexpr int64_t LOG_IO_WAIT_EVENT_TIMEOUT_MS = 100;

}  // namespace palf
}  // namespace oceanbase
#endif
