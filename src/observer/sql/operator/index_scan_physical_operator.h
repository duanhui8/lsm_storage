/* Stub */
#pragma once
#include "sql/operator/physical_operator.h"
#include "common/types.h"
class IndexScanPhysicalOperator : public PhysicalOperator {
public:
  IndexScanPhysicalOperator(void *, void *, ReadWriteMode, const Value*, bool, const Value*, bool) {}
  virtual ~IndexScanPhysicalOperator() = default;
  PhysicalOperatorType type() const override { return PhysicalOperatorType::INDEX_SCAN; }
  RC open(Trx *) override { return RC::SUCCESS; }
  RC next() override { return RC::RECORD_EOF; }
  RC close() override { return RC::SUCCESS; }
  Tuple *current_tuple() override { return nullptr; }
};
