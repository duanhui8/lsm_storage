/* Stub — Delete not yet reimplemented after adapter removal */
#pragma once
#include "sql/operator/physical_operator.h"
class DeletePhysicalOperator : public PhysicalOperator {
public:
  DeletePhysicalOperator(void *) {}
  virtual ~DeletePhysicalOperator() = default;
  PhysicalOperatorType type() const override { return PhysicalOperatorType::DELETE; }
  RC open(Trx *) override { return RC::SUCCESS; }
  RC next() override { return RC::RECORD_EOF; }
  RC close() override { return RC::SUCCESS; }
  Tuple *current_tuple() override { return nullptr; }
};
