/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/ob_log_base_type.h */

#ifndef OCEANBASE_LOGSERVICE_OB_LOG_BASE_TYPE_
#define OCEANBASE_LOGSERVICE_OB_LOG_BASE_TYPE_

#include <cstring>
#include "palf/lsn.h"

namespace oceanbase {
namespace logservice {

// ==================== Log Base Types ========================
// Everyone who writes to CLOG registers a type here.
// Simplified from OB 4.4.2 ob_log_base_type.h — keeps only types relevant to mystorage.
enum ObLogBaseType {
  INVALID_LOG_BASE_TYPE = 0,
  TRANS_SERVICE_LOG_BASE_TYPE = 1,     // Transaction service (put/delete)
  TABLET_OP_LOG_BASE_TYPE = 2,         // Tablet operations (memtable writes)
  STORAGE_SCHEMA_LOG_BASE_TYPE = 3,    // Storage schema changes
  DDL_LOG_BASE_TYPE = 5,              // DDL operations (from OB 4.4.2)
  GC_LS_LOG_BASE_TYPE = 9,            // LS garbage collection
  MAJOR_FREEZE_LOG_BASE_TYPE = 10,    // Major freeze trigger
  PADDING_LOG_BASE_TYPE = 25,         // Padding entry
  MAX_LOG_BASE_TYPE,
};

inline bool is_valid_log_base_type(const ObLogBaseType &type)
{
  return type > INVALID_LOG_BASE_TYPE && type < MAX_LOG_BASE_TYPE;
}

}  // namespace logservice
}  // namespace oceanbase

#endif
