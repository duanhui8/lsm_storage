/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/sql/resolver/ddl/ */

#pragma once
#include "common/sys/rc.h"
class CreateDatabaseSqlNode;
class CreateDatabaseStmt;

namespace oceanbase { namespace sql {
class ObCreateDatabaseResolver {
public:
  ObCreateDatabaseResolver() = default;
  ~ObCreateDatabaseResolver() = default;
  int resolve(const CreateDatabaseSqlNode &node, CreateDatabaseStmt *&stmt);
};
}}
