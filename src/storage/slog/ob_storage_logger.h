/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/storage/slog/ob_storage_logger.h */

#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace oceanbase {
namespace storage {

/**
 * ObStorageLogParam — parameter for writing a SLOG entry.
 * Simplified from OB 4.4.2.
 */
struct ObStorageLogParam {
  uint64_t     tenant_id_;
  int32_t      log_type_;
  const char  *data_;
  int64_t      data_len_;

  ObStorageLogParam() : tenant_id_(1), log_type_(0), data_(nullptr), data_len_(0) {}
};

/**
 * ObStorageLogEntry — a single SLOG entry on disk.
 * Simplified from OB 4.4.2 ob_storage_log_entry.h.
 *
 * On-disk format:
 *   [uint32: magic = 0x534C4F47 "SLOG"]
 *   [uint32: total_size]
 *   [uint64: tenant_id]
 *   [int32:  log_type]
 *   [int32:  data_len]
 *   [data...]
 */
struct ObStorageLogEntry {
  static const uint32_t MAGIC = 0x534C4F47; // "SLOG"

  uint32_t    magic_;
  uint32_t    total_size_;
  uint64_t    tenant_id_;
  int32_t     log_type_;
  int32_t     data_len_;
  char        data_[0];  // flexible array

  static int32_t get_header_size() { return sizeof(uint32_t)*2 + sizeof(uint64_t) + sizeof(int32_t)*2; }
};

// SLOG log types (from OB 4.4.2 ob_storage_log_struct.h)
// OB 4.4.2: SLOG is only for Tenant/LS/Tablet — NOT for databases
enum class ObStorageLogType : int32_t {
  SLOG_INVALID = 0,
  OB_REDO_LOG_CREATE_TENANT_PREPARE = 2,
  OB_REDO_LOG_CREATE_TENANT_COMMIT = 3,
  OB_REDO_LOG_CREATE_TENANT_ABORT = 4,
  OB_REDO_LOG_DELETE_TENANT_PREPARE = 5,
  OB_REDO_LOG_DELETE_TENANT_COMMIT = 6,
  OB_REDO_LOG_CREATE_LS = 9,
  OB_REDO_LOG_CREATE_LS_COMMIT = 10,
  OB_REDO_LOG_CREATE_LS_ABORT = 11,
  OB_REDO_LOG_DELETE_LS = 13,
  OB_REDO_LOG_DELETE_TABLET = 15,
};

/**
 * ObStorageLogger — Storage Log writer + replayer.
 * Simplified from OB 4.4.2 ob_storage_logger.h.
 *
 * Writes SLOG entries to disk, replays on recovery.
 * SLOG is a sequential append-only file, simpler than CLOG (no Paxos).
 */
class ObStorageLogger {
public:
  ObStorageLogger();
  ~ObStorageLogger();

  int init(const char *log_dir);
  void destroy();

  /** Write a single SLOG entry. Returns 0 on success. */
  int write_log(const ObStorageLogParam &param);

  /** Replay all SLOG entries, calling callback for each. */
  int replay(void *ctx, int (*callback)(void *ctx, const ObStorageLogEntry *entry));

  const char *get_dir() const { return slog_dir_.c_str(); }

private:
  std::string slog_dir_;
  std::string slog_file_path_;
  int         fd_ = -1;
  bool        is_inited_ = false;
};

}  // namespace storage
}  // namespace oceanbase
