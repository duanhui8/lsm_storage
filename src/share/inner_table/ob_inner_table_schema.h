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
 * ObInnerTableSchema — inner table schema definitions.
 * Each static method defines one system table.
 * From OB 4.4.2 ob_inner_table_schema.h:342-3660.
 */
class ObInnerTableSchema {
public:
  ObInnerTableSchema() = delete;

  static int all_core_table_schema(schema::ObTableSchema &table_schema);
  static int all_table_schema(schema::ObTableSchema &table_schema);
  static int all_column_schema(schema::ObTableSchema &table_schema);
  static int all_ddl_operation_schema(schema::ObTableSchema &table_schema);
  static int all_database_schema(schema::ObTableSchema &table_schema);
};

// OB 4.4.2 ob_inner_table_schema.h:3663
typedef int (*schema_create_func)(schema::ObTableSchema &table_schema);

// OB 4.4.2 ob_inner_table_schema.h:3669
const schema_create_func core_table_schema_creators[] = {
  ObInnerTableSchema::all_table_schema,
  ObInnerTableSchema::all_column_schema,
  ObInnerTableSchema::all_ddl_operation_schema,
  NULL,
};

// OB 4.4.2 ob_inner_table_schema.h:3675
const schema_create_func sys_table_schema_creators[] = {
  ObInnerTableSchema::all_database_schema,
  NULL,
};

}  // namespace share
}  // namespace oceanbase
