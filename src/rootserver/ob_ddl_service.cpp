/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/rootserver/ob_ddl_service.cpp */

#include "rootserver/ob_ddl_service.h"
#include "share/schema/ob_schema_service.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "storage/logservice/ob_log_base_header.h"
#include "storage/logservice/palf/palf_handle.h"
#include "common/log/log.h"
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>

namespace oceanbase {
namespace rootserver {

ObDDLService &ObDDLService::instance() {
  static ObDDLService s;
  return s;
}

int ObDDLService::init(const char *base_dir) {
  if (is_inited_) return 0;
  base_dir_ = base_dir;
  ::mkdir(base_dir_.c_str(), 0755);

  log_service_ = std::make_unique<logservice::ObLogService>();
  int ret = log_service_->init(base_dir_.c_str());
  if (ret != 0) return ret;
  ret = log_service_->start();
  if (ret != 0) return ret;

  ret = log_service_->open_ls(DDL_LS_ID, log_handler_);
  if (ret != 0) return ret;

  log_handler_->register_replay_handler(logservice::DDL_LOG_BASE_TYPE, this);

  // OB 4.4.2: create or load inner tablets
  // Check if CLOG data exists — if yes, tablets already on disk, just load
  bool need_bootstrap = (::access((base_dir_ + "/clog/1/0").c_str(), F_OK) != 0);
  LOG_INFO("ObDDLService: need_bootstrap=%d (CLOG exists=%d)", need_bootstrap, !need_bootstrap);

  // OB 4.4.2: all inner tablets share SYS_LS (ls_id=1), one CLOG stream
  auto open_inner_tablet = [&](const char *name, uint64_t tablet_id) -> storage::ObTablet * {
    auto *t = new storage::ObTablet();
    // Pass log_service=nullptr: tablets share SYS_LS via register_replay_handler below
    if (t->init(static_cast<int64_t>(tablet_id), nullptr) != 0) {
      delete t; return nullptr;
    }
    system_tablets_[name] = t;
    return t;
  };

  open_inner_tablet("__all_database",      share::OB_ALL_DATABASE_TID);
  open_inner_tablet("__all_ddl_operation", share::OB_ALL_DDL_OPERATION_TID);
  open_inner_tablet("__all_table",         share::OB_ALL_TABLE_TID);
  open_inner_tablet("__all_column",        share::OB_ALL_COLUMN_TID);
  open_inner_tablet("__all_core_table",    2);

  // Wire tablet map to ObDDLOperator services
  ddl_operator_.set_system_tablets(&system_tablets_);
  ddl_operator_.set_log_handler(log_handler_);
  need_bootstrap_ = need_bootstrap;

  // Register all inner tablet MemTables with SYS_LS log handler for PALF replay
  if (log_handler_ != nullptr) {
    for (auto &pair : system_tablets_) {
      auto *t = pair.second;
      if (t) {
        auto *mt = t->get_table_store()->get_active_memtable();
        if (mt) log_handler_->register_replay_handler(
            logservice::TABLET_OP_LOG_BASE_TYPE, mt);
      }
    }
  }

  // OB 4.4.2 bootstrap (ob_bootstrap.cpp:1042): only on first run
  if (need_bootstrap_) {
    auto &schema = share::schema::ObSchemaService::instance();
    uint64_t sys_id = 0;
    ddl_operator_.create_database("sys", sys_id);
    uint64_t sys_db_id = schema.get_database_schema("sys")->get_database_id();

    for (int i = 0; share::core_table_schema_creators[i] != NULL; i++) {
      share::schema::ObTableSchema ts;
      share::core_table_schema_creators[i](ts);
      ts.set_database_id(sys_db_id);
      schema.create_table(ts);
    }
    for (int i = 0; share::sys_table_schema_creators[i] != NULL; i++) {
      share::schema::ObTableSchema ts;
      share::sys_table_schema_creators[i](ts);
      ts.set_database_id(sys_db_id);
      schema.create_table(ts);
    }
    LOG_INFO("ObDDLService: bootstrap complete");
  } else {
    // Restart: reload schemas from system tablets (they were persisted in CLOG + MemTable)
    LOG_INFO("ObDDLService: loading existing tablets (bootstrap skipped)");
    auto &schema = share::schema::ObSchemaService::instance();
    uint64_t sys_db_id = 0;
    if (schema.get_database_schema("sys") != nullptr) {
      sys_db_id = schema.get_database_schema("sys")->get_database_id();
    }
    for (int i = 0; share::core_table_schema_creators[i] != NULL; i++) {
      share::schema::ObTableSchema ts;
      share::core_table_schema_creators[i](ts);
      ts.set_database_id(sys_db_id);
      schema.create_table(ts);
    }
    for (int i = 0; share::sys_table_schema_creators[i] != NULL; i++) {
      share::schema::ObTableSchema ts;
      share::sys_table_schema_creators[i](ts);
      ts.set_database_id(sys_db_id);
      schema.create_table(ts);
    }
  }

  is_inited_ = true;
  LOG_INFO("ObDDLService PALF + SystemTablet inited, base_dir=%s", base_dir);
  return 0;
}

int ObDDLService::create_database(const char *db_name, uint64_t &database_id)
{
  if (!is_inited_ || log_handler_ == nullptr) return -1;

  uint64_t type = 1, name_len = strlen(db_name);
  int64_t buf_len = sizeof(type) + sizeof(name_len) + name_len;
  char *log_buf = static_cast<char *>(std::malloc(buf_len));
  char *p = log_buf;
  std::memcpy(p, &type, sizeof(type)); p += sizeof(type);
  std::memcpy(p, &name_len, sizeof(name_len)); p += sizeof(name_len);
  std::memcpy(p, db_name, name_len);

  palf::LSN lsn; int64_t scn = 0;
  int ret = log_handler_->append(log_buf, buf_len, logservice::DDL_LOG_BASE_TYPE, lsn, scn);
  std::free(log_buf);
  if (ret != 0) { LOG_ERROR("PALF append failed for CREATE DATABASE %s", db_name); return ret; }

  // OB 4.4.2: ObDDLOperator writes to __all_database via ObDatabaseSqlService
  ret = ddl_operator_.create_database(db_name, database_id);
  if (ret != 0) return ret;

  LOG_INFO("ObDDLService: CREATE DATABASE %s (id=%lu) via PALF CLOG, lsn=%lu",
           db_name, database_id, lsn.val_);
  return 0;
}

int ObDDLService::drop_database(const char *db_name)
{
  if (!is_inited_ || log_handler_ == nullptr) return -1;

  uint64_t type = 2, name_len = strlen(db_name);
  int64_t buf_len = sizeof(type) + sizeof(name_len) + name_len;
  char *log_buf = static_cast<char *>(std::malloc(buf_len));
  char *p = log_buf;
  std::memcpy(p, &type, sizeof(type)); p += sizeof(type);
  std::memcpy(p, &name_len, sizeof(name_len)); p += sizeof(name_len);
  std::memcpy(p, db_name, name_len);

  palf::LSN lsn; int64_t scn = 0;
  int ret = log_handler_->append(log_buf, buf_len, logservice::DDL_LOG_BASE_TYPE, lsn, scn);
  std::free(log_buf);
  if (ret != 0) { LOG_ERROR("PALF append failed for DROP DATABASE %s", db_name); return ret; }

  ret = ddl_operator_.drop_database(db_name);
  if (ret != 0) return ret;

  LOG_INFO("ObDDLService: DROP DATABASE %s via PALF CLOG, lsn=%lu", db_name, lsn.val_);
  return 0;
}

int ObDDLService::create_table(share::schema::ObTableSchema &table_schema, uint64_t &table_id)
{
  if (!is_inited_ || log_handler_ == nullptr) return -1;

  const char *tbl_name = table_schema.get_table_name();
  uint64_t type = 3, name_len = strlen(tbl_name), db_id = table_schema.get_database_id();
  int64_t buf_len = sizeof(type) + sizeof(name_len) + name_len + sizeof(db_id);
  char *log_buf = static_cast<char *>(std::malloc(buf_len));
  char *p = log_buf;
  std::memcpy(p, &type, sizeof(type)); p += sizeof(type);
  std::memcpy(p, &name_len, sizeof(name_len)); p += sizeof(name_len);
  std::memcpy(p, tbl_name, name_len); p += name_len;
  std::memcpy(p, &db_id, sizeof(db_id));

  palf::LSN lsn; int64_t scn = 0;
  int ret = log_handler_->append(log_buf, buf_len, logservice::DDL_LOG_BASE_TYPE, lsn, scn);
  std::free(log_buf);
  if (ret != 0) return ret;

  return ddl_operator_.create_table(table_schema, table_id);
}

int ObDDLService::recover_schema()
{
  if (!is_inited_ || log_service_ == nullptr) return 0;

  // PALF CLOG replay for DDL entries
  int ret = log_service_->replay_ls(DDL_LS_ID);
  if (ret != 0) { LOG_WARN("ObDDLService: PALF replay returned %d", ret); }

  // OB 4.4.2: all inner tablets in SYS_LS — replay one CLOG stream

  // Rebuild SchemaService from __all_database tablet (now has replayed data)
  auto *db_tablet = get_system_tablet("__all_database");
  if (db_tablet != nullptr) {
    auto *store = db_tablet->get_table_store();
    storage::ObStoreCtx ctx; ctx.tx_id_ = 1;
    ctx.snapshot_version_ = storage::OB_INVALID_VERSION;
    std::vector<storage::ObStoreRow> rows;
    storage::ObStoreRowkey start_key("", 0);
    storage::ObStoreRowkey end_key("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 8);
    store->scan(ctx, start_key, false, end_key, false, rows);

    auto &schema = share::schema::ObSchemaService::instance();
    for (auto &row : rows) {
      if (row.is_deleted_ || row.row_value_.empty()) continue;
      const char *p = row.row_value_.data();
      uint64_t db_id; std::memcpy(&db_id, p, 8); p += 8;
      int32_t name_len; std::memcpy(&name_len, p, 4); p += 4;
      std::string db_name(p, name_len);
      uint64_t tmp = 0;
      schema.create_database(db_name.c_str(), tmp);
      LOG_INFO("recover: %s (id=%lu) from __all_database tablet", db_name.c_str(), db_id);
    }
  }

  LOG_INFO("ObDDLService: recovery completed");
  return 0;
}

int ObDDLService::replay(const void *buffer, int64_t nbytes,
                          const palf::LSN &lsn, int64_t scn)
{
  (void)lsn; (void)scn;
  if (buffer == nullptr || nbytes <= 0) return -1;

  const char *p = static_cast<const char *>(buffer);
  uint64_t type = 0, name_len = 0, db_id = 0;
  std::memcpy(&type, p, sizeof(type)); p += sizeof(type);
  std::memcpy(&name_len, p, sizeof(name_len)); p += sizeof(name_len);
  char name[256] = {};
  std::memcpy(name, p, name_len); p += name_len;

  if (type == 1) {
    // OB 4.4.2: CLOG replay rebuilds SchemaService from log — no tablet write needed
    uint64_t new_id = 0;
    share::schema::ObSchemaService::instance().create_database(name, new_id);
    LOG_INFO("CLOG replay: CREATE DATABASE %s (id=%lu, lsn=%lu)", name, new_id, lsn.val_);
  } else if (type == 2) {
    share::schema::ObSchemaService::instance().drop_database(name);
    LOG_INFO("CLOG replay: DROP DATABASE %s (lsn=%lu)", name, lsn.val_);
  } else if (type == 3) {
    std::memcpy(&db_id, p, sizeof(db_id));
    share::schema::ObTableSchema ts;
    ts.set_table_name(name);
    ts.set_database_id(db_id);
    uint64_t tid = 0;
    ddl_operator_.create_table(ts, tid);
    LOG_INFO("CLOG replay: CREATE TABLE %s (id=%lu, lsn=%lu)", name, tid, lsn.val_);
  }
  return 0;
}

storage::ObTablet *ObDDLService::get_system_tablet(const char *table_name)
{
  auto it = system_tablets_.find(table_name);
  return (it != system_tablets_.end()) ? it->second : nullptr;
}

}  // namespace rootserver
}  // namespace oceanbase
