/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/rootserver/ob_ddl_operator.h
           /opt/oceanbase/src/rootserver/ob_ddl_operator.cpp (11932 行)

 * ============================================================================
 * ObDDLOperator — 底层 DDL 操作器（对应 OB 4.4.2 ob_ddl_operator.h）
 *
 * 职责: 分配 database_id/table_id + 写入 __all_database 和 __all_ddl_operation 系统表。
 * 每个 CREATE/DROP DATABASE 操作都通过这里写入系统 Tablet 的 MemTable。
 *
 * OB 4.4.2 调用链:
 *   ObDDLOperator::create_database()
 *     → schema_service->fetch_new_database_id()                 分配新 ID
 *     → ObDatabaseSqlService::insert_database()                 INSERT INTO __all_database
 *     → ObDDLSqlService::log_operation(OB_DDL_CREATE_DATABASE)  INSERT INTO __all_ddl_operation
 * ============================================================================
 */
#pragma once
#include <cstdint>
#include <unordered_map>
#include <string>
#include "common/sys/rc.h"
#include "share/schema/ob_database_schema.h"
#include "share/schema/ob_table_schema.h"
#include "share/schema/ob_database_sql_service.h"
#include "share/schema/ob_ddl_sql_service.h"

namespace oceanbase { namespace storage { class ObTablet; } }
namespace oceanbase { namespace logservice { class ObLogHandler; } }

namespace oceanbase { namespace rootserver {
class ObDDLOperator {
public:
  ObDDLOperator() = default;
  ~ObDDLOperator() = default;
  int create_database(const char *db_name, uint64_t &database_id);
  int drop_database(const char *db_name);
  int create_table(share::schema::ObTableSchema &table_schema, uint64_t &table_id);

  /** Set the tablet map — each inner table has its own tablet */
  void set_system_tablets(std::unordered_map<std::string, storage::ObTablet *> *tablets) {
    db_sql_service_.set_system_tablets(reinterpret_cast<std::unordered_map<std::string, void *> *>(tablets));
    ddl_sql_service_.set_system_tablets(reinterpret_cast<std::unordered_map<std::string, void *> *>(tablets));
  }
  void set_log_handler(logservice::ObLogHandler *handler) { log_handler_ = handler; }

private:
  share::schema::ObDatabaseSqlService db_sql_service_;
  share::schema::ObDDLSqlService       ddl_sql_service_;
  logservice::ObLogHandler            *log_handler_ = nullptr;
};
}}
