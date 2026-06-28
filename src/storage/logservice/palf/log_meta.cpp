/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/log_meta.h */

#include "log_meta.h"

namespace oceanbase {
namespace palf {

int LogConfigMeta::init(int64_t proposal_id, const LSN &prev_lsn,
                         int64_t prev_log_proposal_id, int64_t prev_mode_pid,
                         const LogConfigVersion &config_version)
{
  proposal_id_ = proposal_id;
  prev_lsn_ = prev_lsn;
  prev_log_proposal_id_ = prev_log_proposal_id;
  prev_mode_pid_ = prev_mode_pid;
  config_version_ = config_version;
  member_count_ = 1; // single-node
  return 0;
}

void LogConfigMeta::reset() { proposal_id_ = 0; prev_lsn_.reset(); prev_log_proposal_id_ = 0; prev_mode_pid_ = 0; config_version_.reset(); member_count_ = 0; }
bool LogConfigMeta::is_valid() const { return proposal_id_ > 0; }

}  // namespace palf
}  // namespace oceanbase
