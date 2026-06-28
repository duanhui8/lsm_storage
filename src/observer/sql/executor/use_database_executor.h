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

#include "common/log/log.h"
#include "common/sys/rc.h"
#include "event/session_event.h"
#include "event/sql_event.h"
#include "session/session.h"
#include "sql/stmt/use_database_stmt.h"

class UseDatabaseExecutor
{
public:
  UseDatabaseExecutor()          = default;
  virtual ~UseDatabaseExecutor() = default;

  RC execute(SQLStageEvent *sql_event)
  {
    Stmt *stmt = sql_event->stmt();
    ASSERT(stmt->type() == StmtType::USE_DATABASE,
        "use database executor can not run this command: %d",
        static_cast<int>(stmt->type()));

    UseDatabaseStmt *use_db_stmt = static_cast<UseDatabaseStmt *>(stmt);
    const string    &db_name     = use_db_stmt->db_name();

    Session *session = sql_event->session_event()->session();
    session->set_current_db(db_name);

    // Check if the switch succeeded
    if (session->get_current_database_id() == 0) {
      return RC::SCHEMA_DB_NOT_EXIST;
    }

    return RC::SUCCESS;
  }
};
