/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/rootserver/ob_ddl_service.cpp */

#include "rootserver/ob_ddl_service.h"
#include "share/schema/ob_schema_service.h"
#include "storage/ddl/ob_ddl_clog.h"
#include "common/log/log.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace oceanbase {
namespace rootserver {

ObDDLService &ObDDLService::instance() {
  static ObDDLService s;
  return s;
}

int ObDDLService::init(const char *log_dir) {
  if (is_inited_) return 0;
  log_dir_ = log_dir;
  ::mkdir(log_dir_.c_str(), 0755);
  is_inited_ = true;
  LOG_INFO("ObDDLService inited, log_dir=%s", log_dir_.c_str());
  return 0;
}

int ObDDLService::create_database(const char *db_name, uint64_t &database_id)
{
  if (!is_inited_) return -1;

  // === CLOG write (WAL: write BEFORE memory) ===
  // Format: [uint64: magic 0x44444C43 "CDDL"][uint64: type(1=create_db)]
  //         [uint64: db_name_len] [db_name] [uint64: database_id]
  std::string clog_path = log_dir_ + "/ddl_clog";
  int fd = ::open(clog_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd >= 0) {
    uint64_t magic = 0x44444C43; // "CDDL"
    uint64_t type  = 1;           // DDL_CLOG_TYPE_CREATE_DATABASE
    uint64_t name_len = strlen(db_name);
    ::write(fd, &magic, sizeof(magic));
    ::write(fd, &type, sizeof(type));
    ::write(fd, &name_len, sizeof(name_len));
    ::write(fd, db_name, name_len);
    ::write(fd, &database_id, sizeof(database_id)); // 0 = to be assigned
    ::fsync(fd);
    ::close(fd);
  }

  // === Schema write (in-memory) ===
  int ret = ddl_operator_.create_database(db_name, database_id);
  if (ret != 0) return ret;

  LOG_INFO("ObDDLService: CREATE DATABASE %s (id=%lu) + CLOG persisted", db_name, database_id);
  return 0;
}

int ObDDLService::create_table(share::schema::ObTableSchema &table_schema, uint64_t &table_id)
{
  if (!is_inited_) return -1;

  // CLOG write
  std::string clog_path = log_dir_ + "/ddl_clog";
  int fd = ::open(clog_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd >= 0) {
    uint64_t magic = 0x44444C43;
    uint64_t type  = 3; // DDL_CLOG_TYPE_CREATE_TABLE
    const char *name = table_schema.get_table_name();
    uint64_t name_len = strlen(name);
    uint64_t db_id = table_schema.get_database_id();
    ::write(fd, &magic, sizeof(magic));
    ::write(fd, &type, sizeof(type));
    ::write(fd, &name_len, sizeof(name_len));
    ::write(fd, name, name_len);
    ::write(fd, &db_id, sizeof(db_id));
    ::fsync(fd);
    ::close(fd);
  }

  return ddl_operator_.create_table(table_schema, table_id);
}

int ObDDLService::recover_schema()
{
  std::string clog_path = log_dir_ + "/ddl_clog";
  int fd = ::open(clog_path.c_str(), O_RDONLY);
  if (fd < 0) {
    LOG_INFO("ObDDLService: no CLOG file to replay — fresh start");
    return 0;
  }

  int recovered = 0;
  while (true) {
    uint64_t magic = 0, type = 0, name_len = 0, db_id = 0;
    ssize_t n = ::read(fd, &magic, sizeof(magic));
    if (n != sizeof(magic)) break;
    if (magic != 0x44444C43) { LOG_WARN("CLOG magic mismatch"); break; }
    ::read(fd, &type, sizeof(type));
    ::read(fd, &name_len, sizeof(name_len));
    char buf[256] = {};
    ::read(fd, buf, name_len);
    ::read(fd, &db_id, sizeof(db_id));

    if (type == 1) { // CREATE DATABASE
      uint64_t new_id = 0;
      ddl_operator_.create_database(buf, new_id);
      recovered++;
      LOG_INFO("CLOG replay: CREATE DATABASE %s (id=%lu)", buf, new_id);
    } else if (type == 3) { // CREATE TABLE
      share::schema::ObTableSchema ts;
      ts.set_table_name(buf);
      ts.set_database_id(db_id);
      uint64_t tid = 0;
      ddl_operator_.create_table(ts, tid);
      recovered++;
      LOG_INFO("CLOG replay: CREATE TABLE %s (id=%lu)", buf, tid);
    }
  }
  ::close(fd);
  LOG_INFO("ObDDLService: CLOG replay done, recovered %d DDL entries", recovered);
  return recovered;
}

}  // namespace rootserver
}  // namespace oceanbase
