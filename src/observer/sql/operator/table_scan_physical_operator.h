/* Stub */
#pragma once
#include "sql/operator/physical_operator.h"
#include "common/types.h"
class TableScanPhysicalOperator : public PhysicalOperator {
public:
  TableScanPhysicalOperator(void *, ReadWriteMode) {}
  virtual ~TableScanPhysicalOperator() = default;
  PhysicalOperatorType type() const override { return PhysicalOperatorType::TABLE_SCAN; }
  RC open(Trx *) override { return RC::SUCCESS; }
  RC next() override { return RC::RECORD_EOF; }
  RC close() override { return RC::SUCCESS; }
  Tuple *current_tuple() override { return nullptr; }
};
