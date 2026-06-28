/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/share/schema/ob_table_schema.h */

#pragma once

#include <cstdint>
#include <vector>
#include "common/sys/rc.h"

namespace oceanbase {
namespace share {
namespace schema {

class ObColumnSchemaV2;

/**
 * ObTableSchema — in-memory table schema representation.
 * Simplified from OB 4.4.2 ob_table_schema.h.
 */
class ObTableSchema {
public:
  ObTableSchema();
  ~ObTableSchema();

  void reset();

  // setters
  void set_table_id(uint64_t table_id) { table_id_ = table_id; }
  void set_table_name(const char *name);
  void set_database_id(uint64_t database_id) { database_id_ = database_id; }
  void set_schema_version(int64_t schema_version) { schema_version_ = schema_version; }

  // getters
  uint64_t    get_table_id() const { return table_id_; }
  const char *get_table_name() const { return table_name_; }
  uint64_t    get_database_id() const { return database_id_; }
  int64_t     get_schema_version() const { return schema_version_; }
  int64_t     get_column_count() const { return column_count_; }

  // column management
  int add_column(const ObColumnSchemaV2 &column);
  const ObColumnSchemaV2 *get_column(int64_t idx) const;

  // serialize for CLOG
  int serialize(char *buf, int64_t buf_len, int64_t &pos) const;
  int deserialize(const char *buf, int64_t data_len, int64_t &pos);
  int64_t get_serialize_size() const;

private:
  uint64_t table_id_ = 0;
  char     table_name_[128] = {};
  uint64_t database_id_ = 0;
  int64_t  schema_version_ = 0;
  int64_t  column_count_ = 0;

  std::vector<ObColumnSchemaV2> columns_;
};

}  // namespace schema
}  // namespace share
}  // namespace oceanbase
