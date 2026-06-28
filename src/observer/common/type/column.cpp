/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include "common/type/column.h"
#include "common/value.h"

Column::Column() { impl_ = new std::vector<Value>(); }
Column::~Column() { delete static_cast<std::vector<Value>*>(impl_); }
void Column::init(int count, AttrType type) { type_ = type; static_cast<std::vector<Value>*>(impl_)->resize(count); }
Value &Column::get_value(int idx) { return (*static_cast<std::vector<Value>*>(impl_))[idx]; }
const Value &Column::get_value(int idx) const { return (*static_cast<const std::vector<Value>*>(impl_))[idx]; }
int Column::count() const { return static_cast<int>(static_cast<const std::vector<Value>*>(impl_)->size()); }
const char *Column::data() const { return nullptr; }
