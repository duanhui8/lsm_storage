/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/share/inner_table/ob_inner_table_schema.h:342-3660 */

#pragma once

#include "share/schema/ob_table_schema.h"
#include "share/schema/ob_column_schema.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"

namespace oceanbase {
namespace share {

/**
 * ObInnerTableSchema — registers inner table schemas at startup.
 * From OB 4.4.2 ob_inner_table_schema.h:342-3660.
 *
 * Each static method defines the schema for one system table.
 * Called during bootstrap to populate ObSchemaService.
 */
class ObInnerTableSchema {
public:
  ObInnerTableSchema() = delete;

  /** __all_core_table (TID=2) — KV metadata table */
  static int all_core_table_schema(schema::ObTableSchema &table_schema);

  /** __all_table (TID=500001) */
  static int all_table_schema(schema::ObTableSchema &table_schema);

  /** __all_column (TID=500003) */
  static int all_column_schema(schema::ObTableSchema &table_schema);

  /** __all_database (TID=104) */
  static int all_database_schema(schema::ObTableSchema &table_schema);

  /** __all_ddl_operation (TID=5) */
  static int all_ddl_operation_schema(schema::ObTableSchema &table_schema);
};

}  // namespace share
}  // namespace oceanbase
