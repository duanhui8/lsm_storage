/* Stub */
#pragma once
#include "common/sys/rc.h"
#include "sql/stmt/stmt.h"
class InsertStmt : public Stmt {
public:
  InsertStmt() = default;
  InsertStmt(void *, const Value *v, int n) : values_(v), value_amount_(n) {}
  StmtType type() const override { return StmtType::INSERT; }
  static RC create(Db *, const InsertSqlNode &, Stmt *&);
  void *table() const { return table_; }
  const Value *values() const { return values_; }
  int value_amount() const { return value_amount_; }
private:
  void *table_ = nullptr;
  const Value *values_ = nullptr;
  int value_amount_ = 0;
};
