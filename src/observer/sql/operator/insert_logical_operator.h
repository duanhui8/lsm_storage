/* Stub */
#pragma once
#include "sql/operator/logical_operator.h"
#include "sql/parser/parse_defs.h"
class InsertLogicalOperator : public LogicalOperator {
public:
  InsertLogicalOperator(void *, vector<Value> v) : values_(v) {}
  virtual ~InsertLogicalOperator() = default;
  LogicalOperatorType type() const override { return LogicalOperatorType::INSERT; }
  void *table() const { return table_; }
  const vector<Value> &values() const { return values_; }
private:
  void *table_ = nullptr;
  vector<Value> values_;
};
