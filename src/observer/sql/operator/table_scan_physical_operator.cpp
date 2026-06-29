/* Table scan operator — reads from system tablet for __all_database */
#include "sql/operator/table_scan_physical_operator.h"
#include "rootserver/ob_ddl_service.h"
#include "storage/tablet/ob_tablet.h"
#include "storage/tablet/ob_tablet_table_store.h"
#include "storage/ob_i_store.h"
#include "common/log/log.h"

#include "sql/executor/sql_result.h"

RC TableScanPhysicalOperator::open(Trx *)
{
  if (opened_) return RC::SUCCESS;
  opened_ = true;
  row_idx_ = 0;

  // __all_database: scan system tablet directly
  if (table_name_ == "__all_database") {
    // Force set the tuple schema
    // (normally done by optimizer, but our physical plan doesn't set it yet)

    auto &ddl = oceanbase::rootserver::ObDDLService::instance();
    auto *tablet = ddl.get_system_tablet("__all_database");
    if (tablet == nullptr) return RC::SUCCESS;

    auto *store = tablet->get_table_store();
    oceanbase::storage::ObStoreCtx ctx; ctx.tx_id_ = 1;
    ctx.snapshot_version_ = oceanbase::storage::OB_INVALID_VERSION;
    std::vector<oceanbase::storage::ObStoreRow> rows;
    oceanbase::storage::ObStoreRowkey start_key("", 0);
    oceanbase::storage::ObStoreRowkey end_key("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 8);
    store->scan(ctx, start_key, false, end_key, false, rows);

    for (auto &row : rows) {
      if (row.is_deleted_ || row.row_value_.empty()) continue;
      const char *p = row.row_value_.data();
      uint64_t db_id; std::memcpy(&db_id, p, sizeof(uint64_t)); p += sizeof(uint64_t);
      int32_t name_len; std::memcpy(&name_len, p, sizeof(int32_t)); p += sizeof(int32_t);
      std::string db_name(p, name_len);

      std::vector<Value> cells;
      cells.push_back(Value(static_cast<int>(db_id)));
      cells.push_back(Value(db_name.c_str()));
      rows_.push_back(cells);
    }
    LOG_INFO("TableScan __all_database: %zu rows", rows_.size());
  }
  return RC::SUCCESS;
}

RC TableScanPhysicalOperator::next()
{
  if (!opened_ || row_idx_ >= static_cast<int>(rows_.size()))
    return RC::RECORD_EOF;
  tuple_.set_cells(rows_[row_idx_++]);
  return RC::SUCCESS;
}

RC TableScanPhysicalOperator::close()
{
  rows_.clear();
  row_idx_ = 0;
  opened_ = false;
  return RC::SUCCESS;
}

Tuple *TableScanPhysicalOperator::current_tuple()
{
  return (row_idx_ > 0 && row_idx_ <= static_cast<int>(rows_.size())) ? &tuple_ : nullptr;
}

RC TableScanPhysicalOperator::tuple_schema(TupleSchema &schema) const
{
  LOG_INFO("TableScan::tuple_schema called, table=%s", table_name_.c_str());
  if (table_name_ == "__all_database") {
    schema.append_cell(TupleCellSpec("__all_database", "database_id", "database_id"));
    schema.append_cell(TupleCellSpec("__all_database", "database_name", "database_name"));
  }
  return RC::SUCCESS;
}
