/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include "sql/stmt/create_index_stmt.h"
#include "common/log/log.h"

RC CreateIndexStmt::create(Db *, const CreateIndexSqlNode &, Stmt *&) {
  return RC::INTERNAL;
}
