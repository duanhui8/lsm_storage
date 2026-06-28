/* Stub */
#pragma once
#include "sql/operator/physical_operator.h"
class ProjectVecPhysicalOperator : public PhysicalOperator {
public:
  ProjectVecPhysicalOperator() {}
  ProjectVecPhysicalOperator(vector<unique_ptr<Expression>>&&) {}
  virtual ~ProjectVecPhysicalOperator() = default;
  PhysicalOperatorType type() const override { return PhysicalOperatorType::PROJECT_VEC; }
  RC open(Trx *) override { return RC::SUCCESS; }
  RC next() override { return RC::RECORD_EOF; }
  RC close() override { return RC::SUCCESS; }
  Tuple *current_tuple() override { return nullptr; }
};
