/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include "sql/stmt/load_data_stmt.h"
#include "common/log/log.h"

RC LoadDataStmt::create(Db *, const LoadDataSqlNode &, Stmt *&) {
  LOG_WARN("LOAD DATA not yet implemented");
  return RC::UNIMPLEMENTED;
}
