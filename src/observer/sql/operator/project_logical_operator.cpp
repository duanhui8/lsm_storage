/* Stub */
#include "sql/operator/project_logical_operator.h"
ProjectLogicalOperator::ProjectLogicalOperator(vector<unique_ptr<Expression>> &&ex) { expressions() = std::move(ex); }
unique_ptr<LogicalProperty> ProjectLogicalOperator::find_log_prop(const vector<LogicalProperty*> &) { return nullptr; }
