/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include "session/session.h"
#include "storage/schema/ob_schema_service.h"

Session &Session::default_session()
{
  static Session session;
  return session;
}

Session::~Session() {}

void Session::set_current_db(const string &dbname)
{
  auto *schema = oceanbase::share::schema::ObSchemaService::instance().get_database_schema(dbname.c_str());
  if (schema != nullptr) {
    current_db_name_ = dbname;
    current_database_id_ = schema->get_database_id();
  }
}

bool Session::is_trx_multi_operation_mode() const { return trx_multi_operation_mode_; }
void Session::set_trx_multi_operation_mode(bool m) { trx_multi_operation_mode_ = m; }
Trx *Session::current_trx() { return current_trx_; }
void Session::set_current_trx(Trx *trx) { current_trx_ = trx; }
