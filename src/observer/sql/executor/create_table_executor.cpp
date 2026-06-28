/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include "sql/executor/create_table_executor.h"
#include "common/log/log.h"
#include "event/session_event.h"
#include "event/sql_event.h"
#include "session/session.h"
#include "sql/stmt/create_table_stmt.h"
#include "storage/schema/ob_schema_service.h"

using namespace oceanbase::share::schema;

RC CreateTableExecutor::execute(SQLStageEvent *sql_event)
{
  Stmt *stmt = sql_event->stmt();
  Session *session = sql_event->session_event()->session();

  CreateTableStmt *create_table_stmt = static_cast<CreateTableStmt *>(stmt);

  // Build table schema
  ObTableSchema table_schema;
  table_schema.set_table_name(create_table_stmt->table_name().c_str());
  table_schema.set_database_id(session->get_current_database_id());

  // Add columns
  for (const auto &attr : create_table_stmt->attr_infos()) {
    ObColumnSchemaV2 col;
    col.set_column_name(attr.name.c_str());
    col.set_data_type(static_cast<int32_t>(attr.type));
    col.set_data_length(static_cast<int64_t>(attr.length));
    table_schema.add_column(col);
  }

  int ret = ObSchemaService::instance().create_table(table_schema);
  LOG_INFO("Create table %s via SchemaService, table_id=%d",
           create_table_stmt->table_name().c_str(), ret);
  return (ret > 0) ? RC::SUCCESS : RC::INTERNAL;
}
