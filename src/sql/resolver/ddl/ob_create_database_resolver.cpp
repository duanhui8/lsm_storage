/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include "sql/resolver/ddl/ob_create_database_resolver.h"
#include "sql/stmt/create_database_stmt.h"

namespace oceanbase { namespace sql {
int ObCreateDatabaseResolver::resolve(const CreateDatabaseSqlNode &node, CreateDatabaseStmt *&stmt) {
  stmt = new CreateDatabaseStmt(node.db_name);
  return 0;
}
}}
