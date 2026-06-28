/* Stub */
#include "sql/operator/explain_physical_operator.h"
ExplainPhysicalOperator::~ExplainPhysicalOperator() {}
RC ExplainPhysicalOperator::open(Trx *) { return RC::SUCCESS; }
RC ExplainPhysicalOperator::next() { return RC::RECORD_EOF; }
RC ExplainPhysicalOperator::next(void *) { return RC::RECORD_EOF; }
RC ExplainPhysicalOperator::close() { return RC::SUCCESS; }
Tuple *ExplainPhysicalOperator::current_tuple() { return nullptr; }
