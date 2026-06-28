/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#pragma once

#include "common/sys/rc.h"
#include "sql/stmt/create_database_stmt.h"
#include "rootserver/ob_ddl_service.h"
#include "event/session_event.h"
#include "event/sql_event.h"

class CreateDatabaseExecutor
{
public:
  CreateDatabaseExecutor() = default;
  virtual ~CreateDatabaseExecutor() = default;

  RC execute(SQLStageEvent *sql_event)
  {
    Stmt *stmt = sql_event->stmt();
    CreateDatabaseStmt *create_db_stmt = static_cast<CreateDatabaseStmt *>(stmt);
    const string &db_name = create_db_stmt->db_name();
    uint64_t db_id = 0;
    auto &ddl = oceanbase::rootserver::ObDDLService::instance();
    int ret = ddl.create_database(db_name.c_str(), db_id);
    return (ret == 0) ? RC::SUCCESS : RC::INTERNAL;
  }
};
