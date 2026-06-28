/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/rootserver/ob_ddl_operator.cpp */

#include "rootserver/ob_ddl_operator.h"
#include "share/schema/ob_schema_service.h"

namespace oceanbase { namespace rootserver {
int ObDDLOperator::create_database(const char *db_name, uint64_t &database_id) {
  auto &schema = share::schema::ObSchemaService::instance();
  if (schema.get_database_schema(db_name) != nullptr) {
    return -1; // OB_ERR_DB_EXIST
  }
  return schema.create_database(db_name, database_id);
}

int ObDDLOperator::drop_database(const char *db_name) {
  return share::schema::ObSchemaService::instance().drop_database(db_name);
}
int ObDDLOperator::create_table(share::schema::ObTableSchema &table_schema, uint64_t &table_id) {
  int ret = share::schema::ObSchemaService::instance().create_table(table_schema);
  if (ret < 0) return ret;
  table_id = static_cast<uint64_t>(ret);
  return 0;
}
}}
