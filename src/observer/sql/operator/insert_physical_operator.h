/* Stub */
#pragma once
#include "sql/operator/physical_operator.h"
class InsertPhysicalOperator : public PhysicalOperator {
public:
  InsertPhysicalOperator(void *, vector<Value>&&) {}
  virtual ~InsertPhysicalOperator() = default;
  PhysicalOperatorType type() const override { return PhysicalOperatorType::INSERT; }
  RC open(Trx *) override { return RC::SUCCESS; }
  RC next() override { return RC::RECORD_EOF; }
  RC close() override { return RC::SUCCESS; }
  Tuple *current_tuple() override { return nullptr; }
};
