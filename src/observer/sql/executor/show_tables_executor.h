/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#pragma once

#include "common/sys/rc.h"
#include "sql/executor/sql_result.h"
#include "sql/operator/string_list_physical_operator.h"
#include "share/schema/ob_schema_service.h"
#include "event/session_event.h"
#include "event/sql_event.h"
#include "session/session.h"

class ShowTablesExecutor
{
public:
  ShowTablesExecutor() = default;
  virtual ~ShowTablesExecutor() = default;

  RC execute(SQLStageEvent *sql_event)
  {
    SqlResult *sql_result = sql_event->session_event()->sql_result();
    Session *session = sql_event->session_event()->session();

    vector<string> table_names;
    oceanbase::share::schema::ObSchemaService::instance().get_all_tables(
        session->get_current_database_id(), table_names);

    TupleSchema tuple_schema;
    std::string header = std::string("Tables_in_") + std::string(session->get_current_db_name());
    tuple_schema.append_cell(TupleCellSpec("", header.c_str(), header.c_str()));
    sql_result->set_tuple_schema(tuple_schema);

    auto oper = new StringListPhysicalOperator;
    for (const string &s : table_names) oper->append(s);
    sql_result->set_operator(unique_ptr<PhysicalOperator>(oper));
    return RC::SUCCESS;
  }
};
