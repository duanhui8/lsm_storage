/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/share/schema/ob_column_schema.h */

#pragma once

#include <cstdint>
#include <cstring>

namespace oceanbase {
namespace share {
namespace schema {

/**
 * ObColumnSchemaV2 — column schema (single column definition).
 * Simplified from OB 4.4.2 ob_column_schema.h.
 */
class ObColumnSchemaV2 {
public:
  ObColumnSchemaV2();
  ~ObColumnSchemaV2();

  void reset();

  void set_column_id(uint64_t column_id) { column_id_ = column_id; }
  void set_column_name(const char *name);
  void set_data_type(int32_t data_type) { data_type_ = data_type; }
  void set_data_length(int64_t data_length) { data_length_ = data_length; }

  uint64_t    get_column_id() const { return column_id_; }
  const char *get_column_name() const { return column_name_; }
  int32_t     get_data_type() const { return data_type_; }
  int64_t     get_data_length() const { return data_length_; }

  int serialize(char *buf, int64_t buf_len, int64_t &pos) const;
  int deserialize(const char *buf, int64_t data_len, int64_t &pos);
  int64_t get_serialize_size() const;

private:
  uint64_t column_id_ = 0;
  char     column_name_[128] = {};
  int32_t  data_type_ = 0;
  int64_t  data_length_ = 0;
};

}  // namespace schema
}  // namespace share
}  // namespace oceanbase
