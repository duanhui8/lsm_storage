/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/log_define.h */

#ifndef OCEANBASE_LOGSERVICE_LOG_DEFINE_
#define OCEANBASE_LOGSERVICE_LOG_DEFINE_
#include <cstdint>
#include <fcntl.h>
#include <sys/stat.h>

namespace oceanbase {
namespace palf {

// ==================== block and log constants ========================
constexpr int64_t MAX_LOG_HEADER_SIZE = 4 * 1024;           // 4KB
constexpr int64_t MAX_INFO_BLOCK_SIZE = 4 * 1024;           // 4KB
constexpr int64_t MAX_META_ENTRY_SIZE = 4 * 1024;           // 4KB
constexpr int64_t MAX_LOG_BODY_SIZE = 3 * 1024 * 1024 + 512 * 1024;  // 3.5MB
constexpr int64_t PALF_PHY_BLOCK_SIZE = 1 << 26;           // 64MB
constexpr int64_t PALF_BLOCK_SIZE = PALF_PHY_BLOCK_SIZE - MAX_INFO_BLOCK_SIZE;
constexpr int64_t CLOG_FILE_TAIL_PADDING_TRIGGER = 4096;
constexpr int64_t MAX_LOG_BUFFER_SIZE = MAX_LOG_BODY_SIZE + MAX_LOG_HEADER_SIZE + CLOG_FILE_TAIL_PADDING_TRIGGER;
constexpr int64_t LOG_DIO_ALIGN_SIZE = 4 * 1024;

typedef int FileDesc;
typedef uint64_t block_id_t;
typedef uint64_t offset_t;
constexpr int64_t INVALID_PALF_ID = -1;

// ==================== LSN constants ========================
const uint64_t LOG_INVALID_LSN_VAL = UINT64_MAX;
const uint64_t LOG_MAX_LSN_VAL = LOG_INVALID_LSN_VAL - 1;
const uint64_t PALF_INITIAL_LSN_VAL = 0;

// ==================== block ID constants ====================
const block_id_t LOG_INITIAL_BLOCK_ID = 0;
constexpr block_id_t LOG_MAX_BLOCK_ID = UINT64_MAX / PALF_BLOCK_SIZE - 1;
constexpr block_id_t LOG_INVALID_BLOCK_ID = LOG_MAX_BLOCK_ID + 1;

// ==================== log ID ====================
const int64_t FIRST_VALID_LOG_ID = 1;

// ==================== Replica State ========================
enum ObReplicaState {
  INVALID_STATE = 0,
  INIT = 1,
  ACTIVE = 2,
  RECONFIRM = 3,
  PENDING = 4,
};

inline const char *replica_state_to_string(const ObReplicaState &state)
{
  #define CHECK_OB_REPLICA_STATE(x) case(ObReplicaState::x): return #x
  switch (state) {
    CHECK_OB_REPLICA_STATE(INIT);
    CHECK_OB_REPLICA_STATE(ACTIVE);
    CHECK_OB_REPLICA_STATE(RECONFIRM);
    CHECK_OB_REPLICA_STATE(PENDING);
    default: return "INVALID_STATE";
  }
  #undef CHECK_OB_REPLICA_STATE
}

// ==================== Log Type ====================
enum LogType {
  LOG_UNKNOWN = 0,
  LOG_SUBMIT = 201,
  LOG_PADDING = 301,
  LOG_TYPE_MAX = 1000
};

// ==================== Log Replica Type ====================
enum LogReplicaType {
  INVALID_REPLICA = 0,
  NORMAL_REPLICA,
  ARBITRATION_REPLICA,
};

// ==================== Purge Throttling ====================
enum PurgeThrottlingType {
  INVALID_PURGE_TYPE = 0,
  PURGE_BY_RECONFIRM = 1,
  MAX_PURGE_TYPE
};

// ==================== File Suffix ========================
#define FLASHBACK_SUFFIX ".flashback"
#define TMP_SUFFIX ".tmp"

// ==================== Disk IO flags ========================
constexpr int LOG_READ_FLAG = O_RDONLY | O_DIRECT | O_SYNC;
constexpr int LOG_WRITE_FLAG = O_RDWR | O_DIRECT | O_SYNC;
constexpr mode_t FILE_OPEN_MODE = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

// ==================== Utility ====================
inline bool is_valid_log_id(const int64_t log_id) { return log_id > 0; }
inline bool is_valid_block_id(block_id_t block_id) { return block_id >= 0 && block_id < LOG_MAX_BLOCK_ID; }
inline bool is_valid_palf_id(const int64_t id) { return 0 <= id; }
inline bool is_valid_file_desc(const FileDesc &fd) { return 0 <= fd; }

class LSN;

}  // namespace palf
}  // namespace oceanbase

#endif
