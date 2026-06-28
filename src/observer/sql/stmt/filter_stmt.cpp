#include "sql/stmt/filter_stmt.h"
FilterStmt::~FilterStmt() { for (auto *u : filter_units_) delete u; }
RC FilterStmt::create(Db *, void *, unordered_map<string, void *> *, const ConditionSqlNode *, int, FilterStmt *&stmt) { stmt = new FilterStmt(); return RC::SUCCESS; }
RC FilterStmt::create_filter_unit(Db *, void *, unordered_map<string, void *> *, const ConditionSqlNode &, FilterUnit *&unit) { unit = new FilterUnit(); return RC::SUCCESS; }
