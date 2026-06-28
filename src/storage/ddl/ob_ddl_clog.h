/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/storage/ddl/ob_ddl_clog.h */

#pragma once

#include <cstdint>
#include "share/schema/ob_database_schema.h"
#include "share/schema/ob_table_schema.h"

namespace oceanbase {
namespace storage {
namespace ddl {

// DDL CLOG types — from OB 4.4.2 ob_ddl_clog.h
enum class ObDDLClogType : int64_t {
  DDL_CLOG_TYPE_INVALID = 0,
  DDL_CLOG_TYPE_CREATE_DATABASE = 1,
  DDL_CLOG_TYPE_DROP_DATABASE = 2,
  DDL_CLOG_TYPE_CREATE_TABLE = 3,
  DDL_CLOG_TYPE_DROP_TABLE = 4,
};

struct ObDDLClogHeader {
  ObDDLClogType clog_type_;
  int64_t        schema_version_;
  uint64_t       tenant_id_;

  ObDDLClogHeader() : clog_type_(ObDDLClogType::DDL_CLOG_TYPE_INVALID),
                       schema_version_(0), tenant_id_(1) {}
};

// DDL Start — marks the beginning of a DDL operation
struct ObDDLCreateDatabaseLog {
  ObDDLClogHeader                  header_;
  share::schema::ObDatabaseSchema  database_schema_;

  ObDDLCreateDatabaseLog() { header_.clog_type_ = ObDDLClogType::DDL_CLOG_TYPE_CREATE_DATABASE; }
};

// DDL Create Table log
struct ObDDLCreateTableLog {
  ObDDLClogHeader              header_;
  share::schema::ObTableSchema table_schema_;

  ObDDLCreateTableLog() { header_.clog_type_ = ObDDLClogType::DDL_CLOG_TYPE_CREATE_TABLE; }
};

}  // namespace ddl
}  // namespace storage
}  // namespace oceanbase
