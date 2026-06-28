/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/share/schema/ob_ddl_sql_service.h */

#pragma once

#include <cstdint>
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
  void set_system_tablet(void *tablet) { system_tablet_ = tablet; }

private:
  void *system_tablet_ = nullptr;
};

}  // namespace schema
}  // namespace share
}  // namespace oceanbase
