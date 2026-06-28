/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#pragma once

#include "common/sys/rc.h"
#include "sql/stmt/stmt.h"

class Db;
struct CreateIndexSqlNode;

class CreateIndexStmt : public Stmt {
public:
  CreateIndexStmt() = default;
  virtual ~CreateIndexStmt() = default;
  StmtType type() const override { return StmtType::CREATE_INDEX; }
  static RC create(Db *, const CreateIndexSqlNode &, Stmt *&);
};
