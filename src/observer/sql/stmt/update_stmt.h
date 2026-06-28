/* Stub */
#pragma once
#include "common/sys/rc.h"
#include "sql/stmt/stmt.h"
class UpdateStmt : public Stmt {
public:
  UpdateStmt() = default;
  UpdateStmt(void *, Value *, int) {}
  static RC create(Db *, const UpdateSqlNode &, Stmt *&);
  void *table() const { return table_; }
  Value *values() const { return values_; }
  int value_amount() const { return value_amount_; }
private:
  void *table_ = nullptr;
  Value *values_ = nullptr;
  int value_amount_ = 0;
};
