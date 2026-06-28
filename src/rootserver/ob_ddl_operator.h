/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/rootserver/ob_ddl_operator.h */

#pragma once
#include <cstdint>
#include "common/sys/rc.h"
#include "share/schema/ob_database_schema.h"
#include "share/schema/ob_table_schema.h"

namespace oceanbase { namespace rootserver {
class ObDDLOperator {
public:
  ObDDLOperator() = default;
  ~ObDDLOperator() = default;
  int create_database(const char *db_name, uint64_t &database_id);
  int create_table(share::schema::ObTableSchema &table_schema, uint64_t &table_id);
};
}}
