/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/rootserver/ob_ddl_service.h */

#pragma once

#include "common/sys/rc.h"
#include "rootserver/ob_ddl_operator.h"

namespace oceanbase {
namespace rootserver {

/**
 * ObDDLService — DDL orchestrator (matching OB 4.4.2 ob_ddl_service.h).
 * Coordinates: schema operations + CLOG writing + tablet creation.
 */
class ObDDLService {
public:
  ObDDLService() = default;
  ~ObDDLService() = default;

  int init();

  /** create_database — full CREATE DATABASE flow */
  int create_database(const char *db_name, uint64_t &database_id);
  int create_table(share::schema::ObTableSchema &table_schema, uint64_t &table_id);

private:
  ObDDLOperator ddl_operator_;
};

}  // namespace rootserver
}  // namespace oceanbase
