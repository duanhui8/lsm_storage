/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include <string>
#include "sql/stmt/stmt.h"
#include "sql/parser/parse_defs.h"

using namespace std;

class UseDatabaseStmt : public Stmt
{
public:
  UseDatabaseStmt(const string &db_name) : db_name_(db_name) {}
  virtual ~UseDatabaseStmt() = default;

  StmtType type() const override { return StmtType::USE_DATABASE; }

  const string &db_name() const { return db_name_; }

  static RC create(Db *db, const UseDatabaseSqlNode &node, Stmt *&stmt)
  {
    stmt = new UseDatabaseStmt(node.db_name);
    return RC::SUCCESS;
  }

private:
  string db_name_;
};
