#include "sql/stmt/select_stmt.h"
#include "share/schema/ob_schema_service.h"
#include "common/log/log.h"

SelectStmt::~SelectStmt() {}

RC SelectStmt::create(Db *, SelectSqlNode &select_sql, Stmt *&stmt)
{
  auto *sel = new SelectStmt();

  // Check if any of the relations is __all_database (system table)
  for (auto &rel : select_sql.relations) {
    auto *schema = oceanbase::share::schema::ObSchemaService::instance()
        .get_table_schema(rel.c_str());
    if (schema != nullptr) {
      // Register as a known table
      sel->tables_.push_back(const_cast<char *>(rel.c_str()));
      sel->table_names_.push_back(rel);
      LOG_INFO("SelectStmt: resolved table %s", rel.c_str());
    }
  }

  stmt = sel;
  return RC::SUCCESS;
}
