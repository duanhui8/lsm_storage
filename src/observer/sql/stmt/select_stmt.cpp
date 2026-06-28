#include "sql/stmt/select_stmt.h"
SelectStmt::~SelectStmt() {}
RC SelectStmt::create(Db *, SelectSqlNode &, Stmt *&stmt) {
  stmt = new SelectStmt();
  return RC::SUCCESS;
}
