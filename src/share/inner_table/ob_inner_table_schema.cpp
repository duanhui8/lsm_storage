/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/share/inner_table/ob_inner_table_schema.h */

#include "share/inner_table/ob_inner_table_schema.h"

namespace oceanbase {
namespace share {

int ObInnerTableSchema::all_database_schema_(schema::ObTableSchema &table_schema)
{
  // __all_database column definitions (from OB 4.4.2)
  // OB 4.4.2: tenant_id(INTS), database_id(INTS), database_name(VARCHAR)
  table_schema.set_table_id(OB_ALL_DATABASE_TID);
  table_schema.set_table_name(OB_ALL_DATABASE_TNAME);
  table_schema.set_schema_version(1);

  schema::ObColumnSchemaV2 col_tenant, col_db_id, col_db_name;
  col_tenant.set_column_id(OB_ALL_DATABASE_COL_TENANT_ID);
  col_tenant.set_column_name("tenant_id");
  col_tenant.set_data_type(0);  // INTS
  col_tenant.set_data_length(8);
  table_schema.add_column(col_tenant);

  col_db_id.set_column_id(OB_ALL_DATABASE_COL_DATABASE_ID);
  col_db_id.set_column_name("database_id");
  col_db_id.set_data_type(0);  // INTS
  col_db_id.set_data_length(8);
  table_schema.add_column(col_db_id);

  col_db_name.set_column_id(OB_ALL_DATABASE_COL_DATABASE_NAME);
  col_db_name.set_column_name("database_name");
  col_db_name.set_data_type(1);  // CHARS
  col_db_name.set_data_length(128);
  table_schema.add_column(col_db_name);

  return 0;
}

int ObInnerTableSchema::all_core_table_schema(schema::ObTableSchema &table_schema)
{
  return all_database_schema_(table_schema);
}

}  // namespace share
}  // namespace oceanbase
