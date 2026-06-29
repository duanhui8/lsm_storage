/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/share/schema/ob_ddl_sql_service.h */

/* ========== ObDDLSqlService — __all_ddl_operation 系统表写入(对应 OB 4.4.2 share/schema/ob_ddl_sql_service.h)
 * OB 4.4.2: 基类 ObDDLSqlService::log_operation() 写入 __all_ddl_operation,
 *          ObDatabaseSqlService/ObTableSqlService 继承它。
 * MiniOB: log_operation() 直接写系统 Tablet MemTable。
 * ========== */
#pragma once

#include <cstdint>
#include <unordered_map>
#include <string>
#include "share/inner_table/ob_inner_table_schema_constants.h"

namespace oceanbase {
namespace share {
namespace schema {

/**
 * ObDDLSqlService — writes DDL operation logs to __all_ddl_operation.
 * Simplified from OB 4.4.2 ob_schema_service_sql_impl.h.
 *
 * Each DDL operation records: [operation_type 8B][schema_version 8B][db_name_len 4B][db_name]
 */
class ObDDLSqlService {
public:
  ObDDLSqlService() = default;
  ~ObDDLSqlService() = default;

  /** log_operation — INSERT INTO __all_ddl_operation.
   *  OB 4.4.2: records DDL type + schema_version for async schema sync.
   *  @param op_type    ObDDLOperationType enum
   *  @param db_name    database name involved
   */
  int log_operation(ObDDLOperationType op_type, const char *db_name);

  /** Set the system tablet for direct write */
  void set_system_tablets(std::unordered_map<std::string, void *> *tablets) { system_tablets_ = tablets; }

private:
  std::unordered_map<std::string, void *> *system_tablets_ = nullptr;
};

}  // namespace schema
}  // namespace share
}  // namespace oceanbase
