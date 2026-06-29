/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/share/schema/ob_schema_service.h (1491 行)

 * ============================================================================
 * ObSchemaService — Schema 管理服务（对应 OB 4.4.2 同名文件）
 *
 * OB 4.4.2 机制:
 *   ObSchemaService 是租户级别的 schema 管理器，持有所有 database/table/column 的
 *   ObTableSchema 对象。Schema 存储在 MySQL 系统表(__all_database, __all_table等)中，
 *   通过 ObServerSchemaService 定时刷新到各 Observer 的内存 cache。
 *
 * MiniOB 实现:
 *   内存 unordered_map 存储 database/table schema。
 *   启动时从 CLOG 回放 + 系统 Tablet 扫描恢复。
 *   CREATE DATABASE 写入 CLOG → ObDatabaseSqlService → __all_database 系统 Tablet。
 *
 * 关键方法:
 *   create_database / drop_database — 数据库 CRUD
 *   create_table / get_table_schema — 表 schema 管理
 *   get_all_databases / get_all_tables — SHOW DATABASES/TABLES 查询
 * ============================================================================
 */
#pragma once

#include "ob_table_schema.h"
#include "ob_column_schema.h"
#include "ob_database_schema.h"
#include "common/sys/rc.h"
#include <unordered_map>
#include <string>
#include <mutex>

namespace oceanbase {
namespace share {
namespace schema {

/**
 * ObSchemaService — schema management singleton.
 * Simplified from OB 4.4.2 ob_schema_service.h.
 *
 * Manages all database/table schemas in memory.
 * CREATE DATABASE/TABLE go through CLOG first, then update this service.
 */
class ObSchemaService {
public:
  static ObSchemaService &instance();

  // === database ===
  int create_database(const char *db_name, uint64_t &database_id);
  int drop_database(const char *db_name);
  const ObDatabaseSchema *get_database_schema(const char *db_name) const;
  const ObDatabaseSchema *get_database_schema(uint64_t database_id) const;
  int get_all_databases(std::vector<std::string> &db_names) const;

  // === table ===
  int create_table(ObTableSchema &table_schema);
  int drop_table(uint64_t database_id, const char *table_name);
  const ObTableSchema *get_table_schema(const char *table_name) const;
  int get_all_tables(uint64_t database_id, std::vector<std::string> &table_names) const;

  // === CLOG replay ===
  int replay_ddl(const void *buf, int64_t buf_len);

private:
  ObSchemaService() = default;

  uint64_t next_database_id_ = 1;
  uint64_t next_table_id_ = 1;

  mutable std::mutex mutex_;
  std::unordered_map<uint64_t, ObDatabaseSchema> databases_;
  std::unordered_map<uint64_t, ObTableSchema> tables_;  // keyed by table_id
};

}  // namespace schema
}  // namespace share
}  // namespace oceanbase
