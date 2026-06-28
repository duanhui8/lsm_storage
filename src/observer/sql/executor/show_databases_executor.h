/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#pragma once

#include "common/sys/rc.h"
#include "sql/executor/sql_result.h"
#include "sql/operator/string_list_physical_operator.h"
#include "share/schema/ob_schema_service.h"
#include "event/session_event.h"
#include "event/sql_event.h"

class ShowDatabasesExecutor
{
public:
  ShowDatabasesExecutor() = default;
  virtual ~ShowDatabasesExecutor() = default;

  RC execute(SQLStageEvent *sql_event)
  {
    SqlResult *sql_result = sql_event->session_event()->sql_result();
    vector<string> db_names;
    oceanbase::share::schema::ObSchemaService::instance().get_all_databases(db_names);

    TupleSchema tuple_schema;
    tuple_schema.append_cell(TupleCellSpec("", "Database", "Database"));
    sql_result->set_tuple_schema(tuple_schema);

    auto oper = new StringListPhysicalOperator;
    for (const string &s : db_names) oper->append(s);
    sql_result->set_operator(unique_ptr<PhysicalOperator>(oper));
    return RC::SUCCESS;
  }
};
