/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include "sql/executor/ob_create_database_executor.h"

namespace oceanbase { namespace sql {

int ObCreateDatabaseExecutor::execute(const char *db_name, uint64_t &database_id) {
  ddl_service_.init();
  return ddl_service_.create_database(db_name, database_id);
}

}}
