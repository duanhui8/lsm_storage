// Minimal stubs — expression evaluation to be reimplemented with storage/ types
#include "sql/expr/expression.h"
#include "sql/expr/tuple.h"

Expression::~Expression() {}
RC FieldExpr::get_value(const Tuple &tuple, Value &value) const { return tuple.cell_at(field_idx_, value); }
bool FieldExpr::equal(const Expression &other) const {
  if (other.type() != ExprType::FIELD) return false;
  return field_idx_ == static_cast<const FieldExpr &>(other).field_idx_;
}
ComparisonExpr::ComparisonExpr(CompOp c, unique_ptr<Expression> l, unique_ptr<Expression> r)
    : comp_(c), left_(std::move(l)), right_(std::move(r)) {}
RC ComparisonExpr::get_value(const Tuple &, Value &) const { return RC::SUCCESS; }
ConjunctionExpr::ConjunctionExpr(Type t, vector<unique_ptr<Expression>> &children) { conjunction_type_ = t; children_.swap(children); }
RC ConjunctionExpr::get_value(const Tuple &, Value &) const { return RC::SUCCESS; }
ArithmeticExpr::ArithmeticExpr(Type t, unique_ptr<Expression> l, unique_ptr<Expression> r)
    : arithmetic_type_(t), left_(std::move(l)), right_(std::move(r)) {}
RC ArithmeticExpr::get_value(const Tuple &, Value &) const { return RC::SUCCESS; }
RC ValueExpr::get_value(const Tuple &, Value &value) const { value = value_; return RC::SUCCESS; }
bool ValueExpr::equal(const Expression &other) const {
  if (other.type() != ExprType::VALUE) return false;
  return value_.compare(static_cast<const ValueExpr &>(other).value_) == 0;
}
RC ValueExpr::get_column(void *, Column &) { return RC::UNIMPLEMENTED; }
RC CastExpr::get_value(const Tuple &, Value &) const { return RC::SUCCESS; }
CastExpr::CastExpr(unique_ptr<Expression> child, AttrType cast_type) : child_(std::move(child)), cast_type_(cast_type) {}

// ArithmeticExpr additional constructors
ArithmeticExpr::ArithmeticExpr(Type t, Expression *l, Expression *r) : arithmetic_type_(t), left_(l), right_(r) {}


// ComparisonExpr vtable anchor
// bool ComparisonExpr::equal(const Expression &) const { return false; }

// ArithmeticExpr vtable anchor
// bool ArithmeticExpr::equal(const Expression &) const { return false; }

// AggregateExpr create_aggregator stub
unique_ptr<Aggregator> AggregateExpr::create_aggregator() const { return nullptr; }

// vtable anchor
ComparisonExpr::~ComparisonExpr() {}
CastExpr::~CastExpr() {}
RC CastExpr::get_column(void *, Column &) { return RC::UNIMPLEMENTED; }
RC CastExpr::try_get_value(Value &) const { return RC::UNIMPLEMENTED; }
RC ArithmeticExpr::get_column(void *, Column &) { return RC::UNIMPLEMENTED; }
RC ArithmeticExpr::try_get_value(Value &) const { return RC::UNIMPLEMENTED; }
AttrType ArithmeticExpr::value_type() const { return AttrType::UNDEFINED; }
bool ArithmeticExpr::equal(const Expression &) const { return false; }
RC ComparisonExpr::try_get_value(Value &) const { return RC::UNIMPLEMENTED; }
RC ComparisonExpr::eval(void *, vector<unsigned char> &) { return RC::UNIMPLEMENTED; }
UnboundAggregateExpr::UnboundAggregateExpr(const char *name, Expression *child) : aggregate_name_(name), child_(child) {}
UnboundAggregateExpr::UnboundAggregateExpr(const char *name, unique_ptr<Expression> child) : aggregate_name_(name), child_(child.release()) {}
