/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/share/schema/ob_database_sql_service.h (314 行)
           /opt/oceanbase/src/share/schema/ob_database_sql_service.cpp

 * ============================================================================
 * ObDatabaseSqlService — __all_database 系统表的 DML 操作（对应 OB 4.4.2 同名文件）
 *
 * OB 4.4.2 机制:
 *   __all_database 是一个真实的系统表（SYSTEM_TABLE, TABLE_LOAD_TYPE_IN_DISK），
 *   存储在 SYS_LS 的 Tablet 上，有完整的 MemTable + SSTable。
 *   通过 MySQL SQL 执行 INSERT/UPDATE/DELETE:
 *     INSERT INTO __all_database (tenant_id, database_id, database_name, ...) VALUES (...)
 *
 * MiniOB 实现:
 *   系统 Tablet (tablet_id=0) 作为 __all_database 的物理存储，
 *   直接写入 MemTable (LSM 路径)，不经过 SQL 层。
 *
 * 对应 OB 4.4.2 的位置:
 *   - ob_schema_service.h:786 — forward declaration
 *   - ob_schema_service_sql_impl.h:1342 — database_service_ 成员
 *   - ob_database_sql_service.cpp:29 — insert_database() 实现
 * ============================================================================
 */
#pragma once

#include <string>
#include <vector>
#include "share/schema/ob_database_schema.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"

namespace oceanbase {
namespace share {
namespace schema {

/**
 * ObDatabaseSqlService — SQL service for __all_database operations.
 * Simplified from OB 4.4.2 ob_database_sql_service.h:31-87.
 *
 * OB 4.4.2 uses MySQL SQL to write to __all_database.
 * MiniOB writes directly to the system tablet's MemTable (LSM path).
 */
class ObDatabaseSqlService {
public:
  ObDatabaseSqlService() = default;
  ~ObDatabaseSqlService() = default;

  /** insert_database — INSERT a database row into __all_database.
   *  @return 0 on success, -1 on error */
  int insert_database(const ObDatabaseSchema &database_schema);

  /** update_database — UPDATE a database row */
  int update_database(const ObDatabaseSchema &database_schema);

  /** delete_database — DELETE a database row */
  int delete_database(const ObDatabaseSchema &database_schema);

  /** query_database — SELECT a database by name.
   *  @return 0 if found, -1 if not found */
  int query_database(const char *db_name, ObDatabaseSchema &out_schema);

  /** get_all_databases — SELECT all databases */
  int get_all_databases(std::vector<std::string> &db_names);

  /** Get the system tablet pointer for direct write */
  void set_system_tablet(void *tablet) { system_tablet_ = tablet; }

private:
  void *system_tablet_ = nullptr;  // storage::ObTablet* for __all_database
};

}  // namespace schema
}  // namespace share
}  // namespace oceanbase
