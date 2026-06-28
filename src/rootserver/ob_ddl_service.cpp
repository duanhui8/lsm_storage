/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/rootserver/ob_ddl_service.cpp */

#include "rootserver/ob_ddl_service.h"
#include "share/schema/ob_schema_service.h"
#include "storage/ddl/ob_ddl_clog.h"

namespace oceanbase {
namespace rootserver {

int ObDDLService::init() { return 0; }

int ObDDLService::create_database(const char *db_name, uint64_t &database_id)
{
  return ddl_operator_.create_database(db_name, database_id);
}

int ObDDLService::create_table(share::schema::ObTableSchema &table_schema, uint64_t &table_id)
{
  return ddl_operator_.create_table(table_schema, table_id);
}

}  // namespace rootserver
}  // namespace oceanbase
