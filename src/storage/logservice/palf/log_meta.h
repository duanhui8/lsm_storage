/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/log_meta.h */

#ifndef OCEANBASE_LOGSERVICE_LOG_META_
#define OCEANBASE_LOGSERVICE_LOG_META_
#include <cstdint>
#include <cstring>
#include "lsn.h"

namespace oceanbase {
namespace palf {

// ==================== LogConfigVersion ========================
struct LogConfigVersion {
  int64_t config_version_;
  int64_t config_seq_;
  LogConfigVersion() : config_version_(0), config_seq_(0) {}
  void reset() { config_version_ = 0; config_seq_ = 0; }
};

// ==================== LogConfigMeta ========================
// Records membership configuration changes.
// Simplified from OB 4.4.2 log_meta_info.h.
struct LogConfigMeta {
  int64_t proposal_id_;
  LSN     prev_lsn_;
  int64_t prev_log_proposal_id_;
  int64_t prev_mode_pid_;
  LogConfigVersion config_version_;
  int32_t member_count_;
  char    member_data_[4096];  // Simplified: store member list as serialized string

  LogConfigMeta() : proposal_id_(0), prev_lsn_(), prev_log_proposal_id_(0),
                    prev_mode_pid_(0), config_version_(), member_count_(0) { member_data_[0] = '\0'; }
  int init(int64_t proposal_id, const LSN &prev_lsn, int64_t prev_log_proposal_id,
           int64_t prev_mode_pid, const LogConfigVersion &config_version);
  void reset();
  bool is_valid() const;
  int64_t get_serialize_size() const { return sizeof(LogConfigMeta); }
};

// ==================== LogSnapshotMeta ========================
// Records a snapshot point in the log.
struct LogSnapshotMeta {
  LSN     snapshot_lsn_;
  int64_t snapshot_version_;

  LogSnapshotMeta() : snapshot_version_(0) {}
  void reset() { snapshot_lsn_.reset(); snapshot_version_ = 0; }
  int64_t get_serialize_size() const { return sizeof(LogSnapshotMeta); }
};

// ==================== LogModeMeta ========================
// Records access mode changes (APPEND, FLASHBACK, etc.)
struct LogModeMeta {
  int64_t proposal_id_;
  int32_t access_mode_;  // 0=APPEND
  bool    is_applied_mode_meta_;

  LogModeMeta() : proposal_id_(0), access_mode_(0), is_applied_mode_meta_(false) {}
  void reset() { proposal_id_ = 0; access_mode_ = 0; is_applied_mode_meta_ = false; }
  int64_t get_serialize_size() const { return sizeof(LogModeMeta); }
};

// ==================== LogMeta ========================
// Aggregated metadata structure for LogEngine.
struct LogMeta {
  LogConfigMeta   config_meta_;
  LogSnapshotMeta snapshot_meta_;
  LogModeMeta     mode_meta_;
  LSN             log_snapshot_lsn_;

  LogMeta() { config_meta_.reset(); snapshot_meta_.reset(); mode_meta_.reset(); log_snapshot_lsn_.reset(); }
  void reset() { config_meta_.reset(); snapshot_meta_.reset(); mode_meta_.reset(); log_snapshot_lsn_.reset(); }
  int64_t get_serialize_size() const { return sizeof(LogConfigMeta) + sizeof(LogSnapshotMeta) + sizeof(LogModeMeta) + sizeof(LSN); }
};

}  // namespace palf
}  // namespace oceanbase
#endif
