/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/rootserver/ob_ddl_operator.cpp */

#include "rootserver/ob_ddl_operator.h"
#include "share/schema/ob_schema_service.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "storage/logservice/ob_log_handler.h"
#include "storage/logservice/palf/lsn.h"

namespace oceanbase { namespace rootserver {
int ObDDLOperator::create_database(const char *db_name, uint64_t &database_id) {
  // OB 4.4.2: check existence via __all_database scan
  auto &schema = share::schema::ObSchemaService::instance();
  if (schema.get_database_schema(db_name) != nullptr) {
    return -1; // OB_ERR_DB_EXIST
  }
  // Allocate ID via schema service
  int ret = schema.create_database(db_name, database_id);
  if (ret != 0) return ret;

  // OB 4.4.2: CLOG (TABLET_OP) before MemTable write
  if (log_handler_ != nullptr) {
    int64_t name_len = strlen(db_name);
    int64_t row_size = sizeof(uint64_t) + sizeof(int32_t) + name_len;
    char *row_buf = static_cast<char *>(std::malloc(row_size));
    char *rp = row_buf;
    std::memcpy(rp, &database_id, sizeof(uint64_t)); rp += sizeof(uint64_t);
    int32_t nl32 = static_cast<int32_t>(name_len);
    std::memcpy(rp, &nl32, sizeof(int32_t)); rp += sizeof(int32_t);
    std::memcpy(rp, db_name, name_len);
    palf::LSN lsn; int64_t scn = 0;
    log_handler_->append(row_buf, row_size, logservice::TABLET_OP_LOG_BASE_TYPE, lsn, scn);
    std::free(row_buf);
  }

  // OB 4.4.2: INSERT INTO __all_database
  share::schema::ObDatabaseSchema db_schema;
  db_schema.set_database_id(database_id);
  db_schema.set_database_name(db_name);
  db_sql_service_.insert_database(db_schema);

  // OB 4.4.2: INSERT INTO __all_ddl_operation (DDL operation log)
  ddl_sql_service_.log_operation(share::ObDDLOperationType::OB_DDL_CREATE_DATABASE, db_name);
  return 0;
}

int ObDDLOperator::drop_database(const char *db_name) {
  // OB 4.4.2: DELETE FROM __all_database via ObDatabaseSqlService
  auto *schema = share::schema::ObSchemaService::instance().get_database_schema(db_name);
  if (schema != nullptr) {
    share::schema::ObDatabaseSchema db_schema;
    db_schema.set_database_id(schema->get_database_id());
    db_schema.set_database_name(db_name);
    db_sql_service_.delete_database(db_schema);
    ddl_sql_service_.log_operation(share::ObDDLOperationType::OB_DDL_DROP_DATABASE, db_name);
  }
  return share::schema::ObSchemaService::instance().drop_database(db_name);
}
int ObDDLOperator::create_table(share::schema::ObTableSchema &table_schema, uint64_t &table_id) {
  int ret = share::schema::ObSchemaService::instance().create_table(table_schema);
  if (ret < 0) return ret;
  table_id = static_cast<uint64_t>(ret);
  return 0;
}
}}
