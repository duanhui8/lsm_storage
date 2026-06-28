/* Stub */
#pragma once
#include "sql/operator/physical_operator.h"
class TableScanVecPhysicalOperator : public PhysicalOperator {
public:
  TableScanVecPhysicalOperator(void *, ReadWriteMode) {}
  virtual ~TableScanVecPhysicalOperator() = default;
  PhysicalOperatorType type() const override { return PhysicalOperatorType::TABLE_SCAN_VEC; }
  RC open(Trx *) override { return RC::SUCCESS; }
  RC next() override { return RC::RECORD_EOF; }
  RC close() override { return RC::SUCCESS; }
  Tuple *current_tuple() override { return nullptr; }
};
