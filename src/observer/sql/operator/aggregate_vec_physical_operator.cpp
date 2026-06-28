/* Stub */
#include "sql/operator/aggregate_vec_physical_operator.h"
AggregateVecPhysicalOperator::AggregateVecPhysicalOperator(vector<Expression*> &&) {}
RC AggregateVecPhysicalOperator::open(Trx *) { return RC::SUCCESS; }
RC AggregateVecPhysicalOperator::next(void *) { return RC::RECORD_EOF; }
RC AggregateVecPhysicalOperator::close() { return RC::SUCCESS; }
