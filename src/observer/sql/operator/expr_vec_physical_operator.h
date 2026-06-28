/* Stub */
#pragma once
#include "sql/operator/physical_operator.h"
class ExprVecPhysicalOperator : public PhysicalOperator {
public:
  ExprVecPhysicalOperator(vector<Expression*>&&) {}
  virtual ~ExprVecPhysicalOperator() = default;
  PhysicalOperatorType type() const override { return PhysicalOperatorType::EXPR_VEC; }
  RC open(Trx *) override { return RC::SUCCESS; }
  RC next() override { return RC::RECORD_EOF; }
  RC close() override { return RC::SUCCESS; }
  Tuple *current_tuple() override { return nullptr; }
};
