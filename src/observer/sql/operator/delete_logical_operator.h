/* Copyright (c) OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#pragma once

#include "sql/operator/logical_operator.h"

class DeleteLogicalOperator : public LogicalOperator
{
public:
  DeleteLogicalOperator(void *) {}
  virtual ~DeleteLogicalOperator() = default;
  LogicalOperatorType type() const override { return LogicalOperatorType::DELETE; }
  void *table() const { return table_; }
private:
  void *table_ = nullptr;
};
