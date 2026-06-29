/* Stub — full implementation pending */
#pragma once
#include "sql/operator/physical_operator.h"
#include "common/types.h"
#include <string>
#include <vector>

class TableScanPhysicalOperator : public PhysicalOperator {
public:
  TableScanPhysicalOperator(void *table, ReadWriteMode) : table_(table) {}
  virtual ~TableScanPhysicalOperator() = default;
  PhysicalOperatorType type() const override { return PhysicalOperatorType::TABLE_SCAN; }
  RC open(Trx *) override;
  RC next() override;
  RC close() override;
  Tuple *current_tuple() override;
  RC tuple_schema(TupleSchema &schema) const override;
  void set_table_name(const std::string &name) { table_name_ = name; }

private:
  void *table_ = nullptr;
  std::string table_name_;
  int row_idx_ = 0;
  bool opened_ = false;
  std::vector<std::vector<Value>> rows_;
  ValueListTuple tuple_;
};
