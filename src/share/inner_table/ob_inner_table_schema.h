/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/share/inner_table/ob_inner_table_schema.h */

#pragma once

#include "share/schema/ob_table_schema.h"
#include "share/schema/ob_column_schema.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"

namespace oceanbase {
namespace share {

/**
 * ObInnerTableSchema — registers inner table schemas at startup.
 * From OB 4.4.2 ob_inner_table_schema.h:342-3660.
 */
class ObInnerTableSchema {
public:
  ObInnerTableSchema() = delete;

  /** all_core_table_schema — register __all_database + __all_table + __all_column */
  static int all_core_table_schema(schema::ObTableSchema &table_schema);

private:
  static int all_database_schema_(schema::ObTableSchema &table_schema);
};

}  // namespace share
}  // namespace oceanbase
