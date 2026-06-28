/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/palf_options.h */

#ifndef OCEANBASE_LOGSERVICE_PALF_OPTIONS_
#define OCEANBASE_LOGSERVICE_PALF_OPTIONS_
#include <cstdint>
#include "log_define.h"

namespace oceanbase {
namespace palf {

enum class AccessMode : int32_t {
  INVALID = 0,
  APPEND = 1,
  RAW_WRITE = 2,
  FLASHBACK = 3,
  PREPARE_FLASHBACK = 4,
  MAX = 5,
};

struct PalfOptions {
  int64_t palf_id_;
  int64_t disk_usage_limit_size_;
  int64_t log_disk_utilization_limit_threshold_;  // percentage
  int64_t log_disk_throttling_percentage_;
  int64_t log_disk_throttling_maximum_duration_;

  PalfOptions() : palf_id_(INVALID_PALF_ID),
                  disk_usage_limit_size_(0),
                  log_disk_utilization_limit_threshold_(0),
                  log_disk_throttling_percentage_(0),
                  log_disk_throttling_maximum_duration_(0) {}
};

struct PalfAppendOptions {
  int64_t ref_scn_;
  bool    need_nonblock_;
  bool    allow_compress_;
  PalfAppendOptions() : ref_scn_(0), need_nonblock_(false), allow_compress_(false) {}
};

}  // namespace palf
}  // namespace oceanbase
#endif
