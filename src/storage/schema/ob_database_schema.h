/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/share/schema/ob_database_schema.h */

#pragma once

#include <cstdint>
#include <cstring>

namespace oceanbase {
namespace share {
namespace schema {

/**
 * ObDatabaseSchema — database level schema.
 * Simplified from OB 4.4.2 ob_database_schema.h.
 */
class ObDatabaseSchema {
public:
  ObDatabaseSchema();
  ~ObDatabaseSchema();

  void reset();

  void set_database_id(uint64_t database_id) { database_id_ = database_id; }
  void set_database_name(const char *name);

  uint64_t    get_database_id() const { return database_id_; }
  const char *get_database_name() const { return database_name_; }

  int serialize(char *buf, int64_t buf_len, int64_t &pos) const;
  int deserialize(const char *buf, int64_t data_len, int64_t &pos);
  int64_t get_serialize_size() const;

private:
  uint64_t database_id_ = 0;
  char     database_name_[128] = {};
};

}  // namespace schema
}  // namespace share
}  // namespace oceanbase
