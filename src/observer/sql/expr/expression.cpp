// Minimal stubs — expression evaluation to be reimplemented with storage/ types
#include "sql/expr/expression.h"
#include "sql/expr/tuple.h"
RC FieldExpr::get_value(const Tuple &tuple, Value &value) const { return tuple.cell_at(field_idx_, value); }
ComparisonExpr::ComparisonExpr(CompOp c, unique_ptr<Expression> l, unique_ptr<Expression> r)
    : comp_(c), left_(std::move(l)), right_(std::move(r)) {}
RC ComparisonExpr::get_value(const Tuple &, Value &) const { return RC::SUCCESS; }
ConjunctionExpr::ConjunctionExpr(Type t, vector<unique_ptr<Expression>> &children) { conjunction_type_ = t; children_.swap(children); }
RC ConjunctionExpr::get_value(const Tuple &, Value &) const { return RC::SUCCESS; }
ArithmeticExpr::ArithmeticExpr(Type t, unique_ptr<Expression> l, unique_ptr<Expression> r)
    : arithmetic_type_(t), left_(std::move(l)), right_(std::move(r)) {}
RC ArithmeticExpr::get_value(const Tuple &, Value &) const { return RC::SUCCESS; }
RC ValueExpr::get_value(const Tuple &, Value &value) const { value = value_; return RC::SUCCESS; }
RC CastExpr::get_value(const Tuple &, Value &) const { return RC::SUCCESS; }
