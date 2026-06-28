/* Stub */
#pragma once
#include "sql/operator/logical_operator.h"
class TableGetLogicalOperator : public LogicalOperator {
public:
  TableGetLogicalOperator(void *, int = 0) {}
  virtual ~TableGetLogicalOperator() = default;
  LogicalOperatorType type() const override { return LogicalOperatorType::TABLE_GET; }
  void *table() const { return table_; }
private:
  void *table_ = nullptr;
};
