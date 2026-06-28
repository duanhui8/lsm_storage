/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include "sql/executor/desc_table_executor.h"
#include "common/log/log.h"
#include "event/session_event.h"
#include "event/sql_event.h"
#include "session/session.h"
#include "sql/operator/string_list_physical_operator.h"
#include "sql/stmt/desc_table_stmt.h"
#include "storage/schema/ob_schema_service.h"

using namespace oceanbase::share::schema;

RC DescTableExecutor::execute(SQLStageEvent *sql_event)
{
  Stmt *stmt = sql_event->stmt();
  SessionEvent *session_event = sql_event->session_event();
  SqlResult *sql_result = session_event->sql_result();

  DescTableStmt *desc_table_stmt = static_cast<DescTableStmt *>(stmt);
  const char *table_name = desc_table_stmt->table_name().c_str();

  const ObTableSchema *table = ObSchemaService::instance().get_table_schema(table_name);
  if (table != nullptr) {
    TupleSchema tuple_schema;
    tuple_schema.append_cell(TupleCellSpec("", "Field", "Field"));
    tuple_schema.append_cell(TupleCellSpec("", "Type", "Type"));
    tuple_schema.append_cell(TupleCellSpec("", "Length", "Length"));
    sql_result->set_tuple_schema(tuple_schema);

    auto oper = new StringListPhysicalOperator;
    for (int i = 0; i < table->get_column_count(); i++) {
      const ObColumnSchemaV2 *col = table->get_column(i);
      if (col) oper->append({col->get_column_name(), "type", std::to_string(col->get_data_length())});
    }
    sql_result->set_operator(unique_ptr<PhysicalOperator>(oper));
  } else {
    sql_result->set_return_code(RC::SCHEMA_TABLE_NOT_EXIST);
    sql_result->set_state_string("Table not exists");
  }
  return RC::SUCCESS;
}
