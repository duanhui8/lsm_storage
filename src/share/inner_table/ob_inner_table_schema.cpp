/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/share/inner_table/ob_inner_table_schema.h */

#include "share/inner_table/ob_inner_table_schema.h"

namespace oceanbase {
namespace share {

int ObInnerTableSchema::all_core_table_schema(schema::ObTableSchema &table_schema)
{
  // __all_core_table (TID=2) — KV metadata table, bootstrapped first
  table_schema.set_table_id(2);
  table_schema.set_table_name("__all_core_table");
  table_schema.set_schema_version(1);
  schema::ObColumnSchemaV2 c1, c2, c3, c4;
  c1.set_column_id(0); c1.set_column_name("table_name"); c1.set_data_type(1); c1.set_data_length(128);
  c2.set_column_id(1); c2.set_column_name("row_id");    c2.set_data_type(0); c2.set_data_length(8);
  c3.set_column_id(2); c3.set_column_name("column_name");c3.set_data_type(1); c3.set_data_length(128);
  c4.set_column_id(3); c4.set_column_name("column_value");c4.set_data_type(1);c4.set_data_length(256);
  table_schema.add_column(c1); table_schema.add_column(c2);
  table_schema.add_column(c3); table_schema.add_column(c4);
  return 0;
}

int ObInnerTableSchema::all_table_schema(schema::ObTableSchema &table_schema)
{
  // __all_table (TID=500001) — all table metadata
  table_schema.set_table_id(OB_ALL_TABLE_TID);
  table_schema.set_table_name(OB_ALL_TABLE_TNAME);
  table_schema.set_schema_version(1);
  schema::ObColumnSchemaV2 c1, c2, c3, c4;
  c1.set_column_id(0); c1.set_column_name("tenant_id");   c1.set_data_type(0); c1.set_data_length(8);
  c2.set_column_id(1); c2.set_column_name("table_id");     c2.set_data_type(0); c2.set_data_length(8);
  c3.set_column_id(2); c3.set_column_name("table_name");   c3.set_data_type(1); c3.set_data_length(128);
  c4.set_column_id(3); c4.set_column_name("database_id");  c4.set_data_type(0); c4.set_data_length(8);
  table_schema.add_column(c1); table_schema.add_column(c2);
  table_schema.add_column(c3); table_schema.add_column(c4);
  return 0;
}

int ObInnerTableSchema::all_column_schema(schema::ObTableSchema &table_schema)
{
  // __all_column (TID=500003) — all column metadata
  table_schema.set_table_id(OB_ALL_COLUMN_TID);
  table_schema.set_table_name(OB_ALL_COLUMN_TNAME);
  table_schema.set_schema_version(1);
  schema::ObColumnSchemaV2 c1, c2, c3, c4, c5;
  c1.set_column_id(0); c1.set_column_name("tenant_id");    c1.set_data_type(0); c1.set_data_length(8);
  c2.set_column_id(1); c2.set_column_name("table_id");      c2.set_data_type(0); c2.set_data_length(8);
  c3.set_column_id(2); c3.set_column_name("column_id");     c3.set_data_type(0); c3.set_data_length(8);
  c4.set_column_id(3); c4.set_column_name("column_name");   c4.set_data_type(1); c4.set_data_length(128);
  c5.set_column_id(4); c5.set_column_name("data_type");     c5.set_data_type(0); c5.set_data_length(4);
  table_schema.add_column(c1); table_schema.add_column(c2);
  table_schema.add_column(c3); table_schema.add_column(c4);
  table_schema.add_column(c5);
  return 0;
}

int ObInnerTableSchema::all_database_schema(schema::ObTableSchema &table_schema)
{
  // __all_database (TID=104) — database metadata
  table_schema.set_table_id(OB_ALL_DATABASE_TID);
  table_schema.set_table_name(OB_ALL_DATABASE_TNAME);
  table_schema.set_schema_version(1);
  schema::ObColumnSchemaV2 c1, c2, c3;
  c1.set_column_id(OB_ALL_DATABASE_COL_TENANT_ID);
  c1.set_column_name("tenant_id");    c1.set_data_type(0); c1.set_data_length(8);
  c2.set_column_id(OB_ALL_DATABASE_COL_DATABASE_ID);
  c2.set_column_name("database_id");  c2.set_data_type(0); c2.set_data_length(8);
  c3.set_column_id(OB_ALL_DATABASE_COL_DATABASE_NAME);
  c3.set_column_name("database_name");c3.set_data_type(1); c3.set_data_length(128);
  table_schema.add_column(c1); table_schema.add_column(c2);
  table_schema.add_column(c3);
  return 0;
}

int ObInnerTableSchema::all_ddl_operation_schema(schema::ObTableSchema &table_schema)
{
  // __all_ddl_operation (TID=5) — DDL operation log
  table_schema.set_table_id(OB_ALL_DDL_OPERATION_TID);
  table_schema.set_table_name(OB_ALL_DDL_OPERATION_TNAME);
  table_schema.set_schema_version(1);
  schema::ObColumnSchemaV2 c1, c2, c3, c4;
  c1.set_column_id(0); c1.set_column_name("tenant_id");       c1.set_data_type(0); c1.set_data_length(8);
  c2.set_column_id(1); c2.set_column_name("operation_type");   c2.set_data_type(0); c2.set_data_length(8);
  c3.set_column_id(2); c3.set_column_name("schema_version");   c3.set_data_type(0); c3.set_data_length(8);
  c4.set_column_id(3); c4.set_column_name("ddl_stmt_str");     c4.set_data_type(1); c4.set_data_length(1024);
  table_schema.add_column(c1); table_schema.add_column(c2);
  table_schema.add_column(c3); table_schema.add_column(c4);
  return 0;
}

}  // namespace share
}  // namespace oceanbase
