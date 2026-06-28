/* Stub */
#pragma once
#include "sql/operator/logical_operator.h"
#include "common/types.h"
class TableGetLogicalOperator : public LogicalOperator {
public:
  TableGetLogicalOperator(void *, ReadWriteMode = ReadWriteMode::READ_WRITE) {}
  virtual ~TableGetLogicalOperator() = default;
  LogicalOperatorType type() const override { return LogicalOperatorType::TABLE_GET; }
  void *table() const { return table_; }
private:
  void *table_ = nullptr;
};
