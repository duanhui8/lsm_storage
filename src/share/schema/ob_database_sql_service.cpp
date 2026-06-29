/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/share/schema/ob_database_sql_service.cpp */

#include "share/schema/ob_database_sql_service.h"
#include "share/schema/ob_schema_service.h"
#include "storage/tablet/ob_tablet.h"
#include "storage/tablet/ob_tablet_table_store.h"
#include "storage/memtable/ob_memtable.h"
#include "common/log/log.h"
#include <cstring>

namespace oceanbase {
namespace share {
namespace schema {

int ObDatabaseSqlService::insert_database(const ObDatabaseSchema &database_schema)
{
  if (system_tablets_ == nullptr) { LOG_WARN("tablet map null"); return -1; }
  auto it = system_tablets_->find("__all_database");
  if (it == system_tablets_->end()) { LOG_WARN("__all_database tablet not found"); return -1; }
  auto *tablet = static_cast<storage::ObTablet *>(it->second);
  auto *mt = tablet->get_table_store()->get_active_memtable();
  if (mt == nullptr) return -1;

  uint64_t db_id = database_schema.get_database_id();
  const char *db_name = database_schema.get_database_name();

  // Serialize: [database_id 8B][name_len 4B][name]
  int64_t name_len = strlen(db_name);
  int64_t row_size = sizeof(uint64_t) + sizeof(int32_t) + name_len;
  char *row_buf = static_cast<char *>(std::malloc(row_size));
  char *rp = row_buf;
  std::memcpy(rp, &db_id, sizeof(uint64_t)); rp += sizeof(uint64_t);
  std::memcpy(rp, &name_len, sizeof(int32_t)); rp += sizeof(int32_t);
  std::memcpy(rp, db_name, name_len);

  storage::ObStoreCtx ctx; ctx.tx_id_ = 1;
  storage::ObStoreRow row;
  storage::ObStoreRowkey rk(reinterpret_cast<const char *>(&db_id), sizeof(db_id));
  row.rowkey_ = rk;
  row.dml_flag_ = blocksstable::ObDmlFlag::DF_INSERT;
  row.row_value_.assign(row_buf, row_buf + row_size);
  std::free(row_buf);

  int ret = mt->set(ctx, row);
  tablet->get_freezer()->try_freeze();
  LOG_INFO("ObDatabaseSqlService: INSERT INTO __all_database (%lu, %s)", db_id, db_name);
  return ret;
}

int ObDatabaseSqlService::update_database(const ObDatabaseSchema &database_schema)
{
  // UPDATE = DELETE old + INSERT new
  delete_database(database_schema);
  return insert_database(database_schema);
}

int ObDatabaseSqlService::delete_database(const ObDatabaseSchema &database_schema)
{
  if (system_tablets_ == nullptr) return -1;
  auto it = system_tablets_->find("__all_database"); if (it == system_tablets_->end()) return -1; auto *tablet = static_cast<storage::ObTablet *>(it->second);
  auto *mt = tablet->get_table_store()->get_active_memtable();
  if (mt == nullptr) return -1;

  uint64_t db_id = database_schema.get_database_id();
  storage::ObStoreCtx ctx; ctx.tx_id_ = 1;
  storage::ObStoreRow row;
  storage::ObStoreRowkey rk(reinterpret_cast<const char *>(&db_id), sizeof(db_id));
  row.rowkey_ = rk;
  row.dml_flag_ = blocksstable::ObDmlFlag::DF_DELETE;
  mt->set(ctx, row);
  LOG_INFO("ObDatabaseSqlService: DELETE FROM __all_database where database_id=%lu", db_id);
  return 0;
}

int ObDatabaseSqlService::query_database(const char *db_name, ObDatabaseSchema &out_schema)
{
  if (system_tablets_ == nullptr) return -1;
  auto it = system_tablets_->find("__all_database"); if (it == system_tablets_->end()) return -1; auto *tablet = static_cast<storage::ObTablet *>(it->second);
  auto *store = tablet->get_table_store();

  storage::ObStoreCtx ctx; ctx.tx_id_ = 1;
  ctx.snapshot_version_ = storage::OB_INVALID_VERSION;
  std::vector<storage::ObStoreRow> rows;
  storage::ObStoreRowkey start_key("", 0);
  storage::ObStoreRowkey end_key("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 8);
  store->scan(ctx, start_key, false, end_key, false, rows);

  for (auto &row : rows) {
    if (row.is_deleted_ || row.row_value_.empty()) continue;
    const char *p = row.row_value_.data();
    uint64_t db_id;
    std::memcpy(&db_id, p, sizeof(uint64_t)); p += sizeof(uint64_t);
    int32_t name_len;
    std::memcpy(&name_len, p, sizeof(int32_t)); p += sizeof(int32_t);
    std::string name(p, name_len);

    if (strcmp(name.c_str(), db_name) == 0) {
      out_schema.set_database_id(db_id);
      out_schema.set_database_name(name.c_str());
      return 0;
    }
  }
  return -1; // not found
}

int ObDatabaseSqlService::get_all_databases(std::vector<std::string> &db_names)
{
  if (system_tablets_ == nullptr) return -1;
  auto it = system_tablets_->find("__all_database"); if (it == system_tablets_->end()) return -1; auto *tablet = static_cast<storage::ObTablet *>(it->second);
  auto *store = tablet->get_table_store();

  storage::ObStoreCtx ctx; ctx.tx_id_ = 1;
  ctx.snapshot_version_ = storage::OB_INVALID_VERSION;
  std::vector<storage::ObStoreRow> rows;
  storage::ObStoreRowkey start_key("", 0);
  storage::ObStoreRowkey end_key("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 8);
  store->scan(ctx, start_key, false, end_key, false, rows);

  for (auto &row : rows) {
    if (row.is_deleted_ || row.row_value_.empty()) continue;
    const char *p = row.row_value_.data();
    p += sizeof(uint64_t); // skip db_id
    int32_t name_len;
    std::memcpy(&name_len, p, sizeof(int32_t)); p += sizeof(int32_t);
    db_names.push_back(std::string(p, name_len));
  }
  return 0;
}

}  // namespace schema
}  // namespace share
}  // namespace oceanbase
