/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#pragma once

#include "common/sys/rc.h"
#include "sql/stmt/drop_database_stmt.h"
#include "share/schema/ob_schema_service.h"
#include "session/session.h"
#include "event/session_event.h"
#include "event/sql_event.h"

class DropDatabaseExecutor
{
public:
  DropDatabaseExecutor() = default;
  virtual ~DropDatabaseExecutor() = default;

  RC execute(SQLStageEvent *sql_event)
  {
    Stmt *stmt = sql_event->stmt();
    DropDatabaseStmt *drop_db_stmt = static_cast<DropDatabaseStmt *>(stmt);
    const string &db_name = drop_db_stmt->db_name();

    Session *session = sql_event->session_event()->session();
    if (0 == strcasecmp(session->get_current_db_name(), db_name.c_str())) {
      session->set_current_db("sys");
    }

    oceanbase::share::schema::ObSchemaService::instance().drop_database(db_name.c_str());
    return RC::SUCCESS;
  }
};
