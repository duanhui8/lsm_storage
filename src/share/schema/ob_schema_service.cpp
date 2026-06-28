/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/share/schema/ */

#include "ob_schema_service.h"
#include <cstring>

namespace oceanbase {
namespace share {
namespace schema {

// ==================== ObColumnSchemaV2 ====================

ObColumnSchemaV2::ObColumnSchemaV2() {}
ObColumnSchemaV2::~ObColumnSchemaV2() {}

void ObColumnSchemaV2::reset() { std::memset(this, 0, sizeof(*this)); }

void ObColumnSchemaV2::set_column_name(const char *name) {
  if (name) { std::strncpy(column_name_, name, sizeof(column_name_) - 1); }
}

int ObColumnSchemaV2::serialize(char *buf, int64_t buf_len, int64_t &pos) const {
  int64_t sz = get_serialize_size();
  if (buf_len - pos < sz) return -1;
  std::memcpy(buf + pos, this, sz);
  pos += sz;
  return 0;
}

int ObColumnSchemaV2::deserialize(const char *buf, int64_t data_len, int64_t &pos) {
  int64_t sz = get_serialize_size();
  if (data_len - pos < sz) return -1;
  std::memcpy(this, buf + pos, sz);
  pos += sz;
  return 0;
}

int64_t ObColumnSchemaV2::get_serialize_size() const { return sizeof(ObColumnSchemaV2); }

// ==================== ObTableSchema ====================

ObTableSchema::ObTableSchema() {}
ObTableSchema::~ObTableSchema() {}

void ObTableSchema::reset() { table_id_ = 0; table_name_[0] = '\0'; database_id_ = 0; schema_version_ = 0; column_count_ = 0; columns_.clear(); }

void ObTableSchema::set_table_name(const char *name) {
  if (name) { std::strncpy(table_name_, name, sizeof(table_name_) - 1); }
}

int ObTableSchema::add_column(const ObColumnSchemaV2 &column) {
  columns_.push_back(column);
  column_count_ = static_cast<int64_t>(columns_.size());
  return 0;
}

const ObColumnSchemaV2 *ObTableSchema::get_column(int64_t idx) const {
  if (idx < 0 || idx >= column_count_) return nullptr;
  return &columns_[idx];
}

int ObTableSchema::serialize(char *buf, int64_t buf_len, int64_t &pos) const {
  int64_t total = get_serialize_size();
  if (buf_len - pos < total) return -1;
  std::memcpy(buf + pos, &table_id_, sizeof(uint64_t)); pos += sizeof(uint64_t);
  std::memcpy(buf + pos, table_name_, sizeof(table_name_)); pos += sizeof(table_name_);
  std::memcpy(buf + pos, &database_id_, sizeof(uint64_t)); pos += sizeof(uint64_t);
  std::memcpy(buf + pos, &schema_version_, sizeof(int64_t)); pos += sizeof(int64_t);
  std::memcpy(buf + pos, &column_count_, sizeof(int64_t)); pos += sizeof(int64_t);
  for (int64_t i = 0; i < column_count_; i++) {
    if (columns_[i].serialize(buf, buf_len, pos) != 0) return -1;
  }
  return 0;
}

int ObTableSchema::deserialize(const char *buf, int64_t data_len, int64_t &pos) {
  if (data_len - pos < static_cast<int64_t>(5 * sizeof(uint64_t))) return -1;
  std::memcpy(&table_id_, buf + pos, sizeof(uint64_t)); pos += sizeof(uint64_t);
  std::memcpy(table_name_, buf + pos, sizeof(table_name_)); pos += sizeof(table_name_);
  std::memcpy(&database_id_, buf + pos, sizeof(uint64_t)); pos += sizeof(uint64_t);
  std::memcpy(&schema_version_, buf + pos, sizeof(int64_t)); pos += sizeof(int64_t);
  std::memcpy(&column_count_, buf + pos, sizeof(int64_t)); pos += sizeof(int64_t);
  columns_.clear();
  for (int64_t i = 0; i < column_count_; i++) {
    ObColumnSchemaV2 col;
    if (col.deserialize(buf, data_len, pos) != 0) return -1;
    columns_.push_back(col);
  }
  return 0;
}

int64_t ObTableSchema::get_serialize_size() const {
  int64_t sz = 5 * sizeof(int64_t);
  for (const auto &col : columns_) sz += col.get_serialize_size();
  return sz;
}

// ==================== ObDatabaseSchema ====================

ObDatabaseSchema::ObDatabaseSchema() {}
ObDatabaseSchema::~ObDatabaseSchema() {}

void ObDatabaseSchema::reset() { std::memset(this, 0, sizeof(*this)); }

void ObDatabaseSchema::set_database_name(const char *name) {
  if (name) { std::strncpy(database_name_, name, sizeof(database_name_) - 1); }
}

int ObDatabaseSchema::serialize(char *buf, int64_t buf_len, int64_t &pos) const {
  int64_t sz = get_serialize_size();
  if (buf_len - pos < sz) return -1;
  std::memcpy(buf + pos, this, sz);
  pos += sz;
  return 0;
}

int ObDatabaseSchema::deserialize(const char *buf, int64_t data_len, int64_t &pos) {
  int64_t sz = get_serialize_size();
  if (data_len - pos < sz) return -1;
  std::memcpy(this, buf + pos, sz);
  pos += sz;
  return 0;
}

int64_t ObDatabaseSchema::get_serialize_size() const { return sizeof(ObDatabaseSchema); }

// ==================== ObSchemaService ====================

ObSchemaService &ObSchemaService::instance() {
  static ObSchemaService s;
  return s;
}

int ObSchemaService::create_database(const char *db_name, uint64_t &database_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  // Dedup: skip if database with same name already exists
  for (auto &pair : databases_) {
    if (std::strcmp(pair.second.get_database_name(), db_name) == 0) {
      database_id = pair.first;
      return 0;
    }
  }
  database_id = next_database_id_++;
  ObDatabaseSchema db;
  db.set_database_id(database_id);
  db.set_database_name(db_name);
  databases_[database_id] = db;
  return 0;
}

int ObSchemaService::drop_database(const char *db_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = databases_.begin(); it != databases_.end(); ++it) {
    if (std::strcmp(it->second.get_database_name(), db_name) == 0) {
      databases_.erase(it);
      return 0;
    }
  }
  return -1;
}

const ObDatabaseSchema *ObSchemaService::get_database_schema(const char *db_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &pair : databases_) {
    if (std::strcmp(pair.second.get_database_name(), db_name) == 0)
      return &pair.second;
  }
  return nullptr;
}

const ObDatabaseSchema *ObSchemaService::get_database_schema(uint64_t database_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = databases_.find(database_id);
  return (it != databases_.end()) ? &it->second : nullptr;
}

int ObSchemaService::get_all_databases(std::vector<std::string> &db_names) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &pair : databases_) db_names.push_back(pair.second.get_database_name());
  return 0;
}

int ObSchemaService::create_table(ObTableSchema &table_schema) {
  std::lock_guard<std::mutex> lock(mutex_);
  uint64_t table_id = next_table_id_++;
  table_schema.set_table_id(table_id);
  table_schema.set_schema_version(1);
  tables_[table_id] = table_schema;
  return static_cast<int>(table_id);
}

int ObSchemaService::drop_table(uint64_t, const char *table_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = tables_.begin(); it != tables_.end(); ++it) {
    if (std::strcmp(it->second.get_table_name(), table_name) == 0) {
      tables_.erase(it);
      return 0;
    }
  }
  return -1;
}

const ObTableSchema *ObSchemaService::get_table_schema(const char *table_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &pair : tables_) {
    if (std::strcmp(pair.second.get_table_name(), table_name) == 0)
      return &pair.second;
  }
  return nullptr;
}

int ObSchemaService::get_all_tables(uint64_t database_id, std::vector<std::string> &table_names) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &pair : tables_) {
    if (pair.second.get_database_id() == database_id)
      table_names.push_back(pair.second.get_table_name());
  }
  return 0;
}

int ObSchemaService::replay_ddl(const void *buf, int64_t buf_len) {
  if (buf == nullptr || buf_len <= 0) return -1;
  const char *ptr = static_cast<const char *>(buf);
  // Simple format: first byte = type (0=db, 1=table)
  if (buf_len < 1) return -1;
  uint8_t type = static_cast<uint8_t>(*ptr++);
  if (type == 0) { // database
    ObDatabaseSchema db;
    int64_t pos = 0;
    if (db.deserialize(ptr, buf_len - 1, pos) != 0) return -1;
    databases_[db.get_database_id()] = db;
  } else if (type == 1) { // table
    ObTableSchema table;
    int64_t pos = 0;
    if (table.deserialize(ptr, buf_len - 1, pos) != 0) return -1;
    tables_[table.get_table_id()] = table;
  }
  return 0;
}

}  // namespace schema
}  // namespace share
}  // namespace oceanbase
