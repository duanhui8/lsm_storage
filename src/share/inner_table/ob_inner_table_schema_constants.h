/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/share/inner_table/ob_inner_table_schema_constants.h */

#pragma once

#include <cstdint>

namespace oceanbase {
namespace share {

// === Inner table IDs (from OB 4.4.2) ===
// "__all_database"
constexpr uint64_t OB_ALL_DATABASE_TID   = 104;
constexpr uint64_t OB_ALL_DATABASE_HISTORY_TID = 105;
// "__all_table"
constexpr uint64_t OB_ALL_TABLE_TID      = 500001;
// "__all_column"
constexpr uint64_t OB_ALL_COLUMN_TID     = 500003;

// === Inner table names (from OB 4.4.2 ob_inner_table_schema_constants.h:3349) ===
constexpr const char *OB_ALL_DATABASE_TNAME = "__all_database";
constexpr const char *OB_ALL_TABLE_TNAME    = "__all_table";
constexpr const char *OB_ALL_COLUMN_TNAME   = "__all_column";

// === __all_database column IDs ===
constexpr int64_t OB_ALL_DATABASE_COL_TENANT_ID           = 0;
constexpr int64_t OB_ALL_DATABASE_COL_DATABASE_ID         = 1;
constexpr int64_t OB_ALL_DATABASE_COL_DATABASE_NAME       = 2;

}  // namespace share
}  // namespace oceanbase
