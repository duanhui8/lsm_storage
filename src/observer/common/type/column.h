/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#pragma once

#include <vector>
#include "common/type/attr_type.h"

class Value;

class Column {
public:
  Column();
  ~Column();
  void init(int count, AttrType type = AttrType::UNDEFINED);
  AttrType attr_type() const { return type_; }
  const char *data() const;
  Value &get_value(int idx);
  const Value &get_value(int idx) const;
  int count() const;
private:
  void *impl_;
  AttrType type_ = AttrType::UNDEFINED;
};
