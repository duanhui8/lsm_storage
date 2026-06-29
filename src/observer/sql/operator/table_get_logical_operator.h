/* Stub */
#pragma once
#include "sql/operator/logical_operator.h"
#include "common/types.h"
#include <string>
class TableGetLogicalOperator : public LogicalOperator {
public:
  TableGetLogicalOperator(void *, ReadWriteMode = ReadWriteMode::READ_WRITE) {}
  virtual ~TableGetLogicalOperator() = default;
  LogicalOperatorType type() const override { return LogicalOperatorType::TABLE_GET; }
  void *table() const { return table_; }
  void set_table_name(const std::string &n) { table_name_ = n; }
  std::string get_table_name() const { return table_name_; }
private:
  void *table_ = nullptr;
  std::string table_name_;
};
