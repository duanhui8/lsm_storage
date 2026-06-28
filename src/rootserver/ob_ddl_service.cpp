/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/rootserver/ob_ddl_service.cpp */

#include "rootserver/ob_ddl_service.h"
#include "share/schema/ob_schema_service.h"
#include "storage/logservice/ob_log_base_header.h"
#include "storage/logservice/palf/palf_handle.h"
#include "storage/blocksstable/ob_sstable_builder.h"
#include "storage/blocksstable/ob_block_header.h"
#include "storage/blocksstable/ob_data_store_desc.h"
#include "common/log/log.h"
#include <cstring>
#include <cstdio>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

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

  // Init PALF-based log service (OB 4.4.2 pattern: ObLogService → PalfEnvImpl → PalfHandleImpl)
  log_service_ = std::make_unique<logservice::ObLogService>();
  int ret = log_service_->init(base_dir_.c_str());
  if (ret != 0) return ret;
  ret = log_service_->start();
  if (ret != 0) return ret;

  // Open Log Stream for DDL (LS_ID=1)
  ret = log_service_->open_ls(DDL_LS_ID, log_handler_);
  if (ret != 0) return ret;

  // Register DDL replay handler (OB 4.4.2 REGISTER_TO_LOGSERVICE pattern)
  log_handler_->register_replay_handler(logservice::DDL_LOG_BASE_TYPE, this);

  // Init SLOG (Storage Log for metadata)
  std::string slog_dir = base_dir_ + "/slog";
  ret = slog_logger_.init(slog_dir.c_str());
  if (ret != 0) { LOG_WARN("Failed to init SLOG"); }

  is_inited_ = true;
  LOG_INFO("ObDDLService PALF + SLOG inited, base_dir=%s", base_dir);
  return 0;
}

int ObDDLService::create_database(const char *db_name, uint64_t &database_id)
{
  if (!is_inited_ || log_handler_ == nullptr) return -1;

  // === CLOG via PALF (write BEFORE schema — WAL pattern) ===
  // Build log buffer: [uint64: type=1] [uint64: name_len] [name]
  uint64_t type = 1, name_len = strlen(db_name);
  int64_t buf_len = sizeof(type) + sizeof(name_len) + name_len;
  char *log_buf = static_cast<char *>(std::malloc(buf_len));
  char *p = log_buf;
  std::memcpy(p, &type, sizeof(type)); p += sizeof(type);
  std::memcpy(p, &name_len, sizeof(name_len)); p += sizeof(name_len);
  std::memcpy(p, db_name, name_len);

  palf::LSN lsn;
  int64_t scn = 0;
  int ret = log_handler_->append(log_buf, buf_len,
      logservice::DDL_LOG_BASE_TYPE, lsn, scn);
  std::free(log_buf);
  if (ret != 0) { LOG_ERROR("PALF append failed for CREATE DATABASE %s", db_name); return ret; }

  // === Schema write (in-memory, after CLOG) ===
  ret = ddl_operator_.create_database(db_name, database_id);
  if (ret != 0) return ret;

  // === Create system tablet SSTable (baseline) ===
  // OB 4.4.2: each database has a system tablet stored in block_file
  std::string sstable_dir = base_dir_ + "/sstable/block";
  std::string mkdir_cmd = "mkdir -p " + sstable_dir;
  ::system(mkdir_cmd.c_str());
  std::string block_file_path = sstable_dir + "/block_file";
  std::string meta_file_path = sstable_dir + "/db_" + std::to_string(database_id) + ".meta";

  // Write a single macro block with DB metadata as micro block
  int fd = ::open(block_file_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
  if (fd >= 0) {
    // Build a minimal macro block
    blocksstable::ObMacroBlockCommonHeader common_hdr;
    blocksstable::ObSSTableMacroBlockHeader sstable_hdr;
    blocksstable::ObMicroBlockHeader micro_hdr;

    // --- Micro block data: serialize ObDatabaseSchema ---
    share::schema::ObDatabaseSchema db_schema;
    db_schema.set_database_id(database_id);
    db_schema.set_database_name(db_name);
    std::string schema_buf;
    schema_buf.resize(db_schema.get_serialize_size());
    int64_t pos = 0;
    db_schema.serialize(const_cast<char *>(schema_buf.data()), schema_buf.size(), pos);

    // --- Micro block header ---
    micro_hdr.reset();
    micro_hdr.magic_ = blocksstable::ObMicroBlockHeader::MICRO_BLOCK_HEADER_MAGIC; // 1005
    micro_hdr.row_count_ = 1;
    micro_hdr.column_count_ = 1;
    micro_hdr.data_length_ = static_cast<int32_t>(schema_buf.size());
    micro_hdr.data_zlength_ = micro_hdr.data_length_;
    micro_hdr.row_store_type_ = 0; // FLAT
    micro_hdr.header_size_ = sizeof(blocksstable::ObMicroBlockHeader);
    micro_hdr.row_index_offset_ = static_cast<uint32_t>(schema_buf.size());

    // --- Write macro block to block_file ---
    // Layout: [common_hdr(24B)][sstable_hdr(~92B)][micro_hdr(~64B)][data]
    static const int64_t MACRO_SIZE = 2 * 1024 * 1024; // 2MB
    char *macro_buf = static_cast<char *>(std::calloc(1, MACRO_SIZE));
    int64_t offset = 0;

    common_hdr.header_size_ = sizeof(blocksstable::ObMacroBlockCommonHeader);
    common_hdr.magic_ = blocksstable::MACRO_BLOCK_COMMON_HEADER_MAGIC; // 1001
    common_hdr.version_ = blocksstable::MACRO_BLOCK_COMMON_HEADER_VERSION; // 1
    common_hdr.attr_ = static_cast<int32_t>(blocksstable::MacroBlockType::SSTableData);

    sstable_hdr.fixed_header_.magic_ = blocksstable::SSTABLE_MACRO_BLOCK_HEADER_MAGIC; // 1007
    sstable_hdr.fixed_header_.tablet_id_ = database_id;
    sstable_hdr.fixed_header_.column_count_ = 1;
    sstable_hdr.fixed_header_.row_count_ = 1;
    sstable_hdr.fixed_header_.occupy_size_ = static_cast<int32_t>(schema_buf.size());
    sstable_hdr.fixed_header_.micro_block_count_ = 1;
    sstable_hdr.fixed_header_.row_store_type_ = 0;

    // First write headers, then micro block
    int64_t micro_offset = sizeof(blocksstable::ObMacroBlockCommonHeader)
                         + sizeof(blocksstable::ObSSTableMacroBlockHeader::FixedHeader);
    sstable_hdr.fixed_header_.micro_block_data_offset_ = static_cast<int32_t>(micro_offset);
    sstable_hdr.fixed_header_.micro_block_data_size_ = static_cast<int32_t>(
        sizeof(blocksstable::ObMicroBlockHeader) + schema_buf.size());
    sstable_hdr.fixed_header_.header_size_ = sizeof(blocksstable::ObSSTableMacroBlockHeader::FixedHeader);

    common_hdr.serialize(macro_buf, MACRO_SIZE, offset);
    sstable_hdr.serialize(macro_buf, MACRO_SIZE, offset);
    micro_hdr.serialize(macro_buf, MACRO_SIZE, offset);
    std::memcpy(macro_buf + offset, schema_buf.data(), schema_buf.size());
    offset += schema_buf.size();

    // Set common header payload info
    int64_t payload_size = offset - sizeof(blocksstable::ObMacroBlockCommonHeader);
    blocksstable::ObMacroBlockCommonHeader *hdr_ptr =
        reinterpret_cast<blocksstable::ObMacroBlockCommonHeader *>(macro_buf);
    hdr_ptr->payload_size_ = static_cast<int32_t>(payload_size);

    ::write(fd, macro_buf, MACRO_SIZE);
    ::fsync(fd);
    ::close(fd);
    std::free(macro_buf);

    LOG_INFO("ObDDLService: created SSTable block for database %s (id=%lu) at %s",
             db_name, database_id, block_file_path.c_str());
  } else {
    LOG_WARN("ObDDLService: failed to create block_file at %s", block_file_path.c_str());
  }

  // === SLOG write (metadata WAL, matching OB 4.4.2) ===
  storage::ObStorageLogParam slog_param;
  slog_param.tenant_id_ = 1;
  slog_param.log_type_  = static_cast<int32_t>(storage::ObStorageLogType::SLOG_CREATE_DATABASE);
  slog_param.data_      = db_name;
  slog_param.data_len_  = name_len;
  slog_logger_.write_log(slog_param);

  LOG_INFO("ObDDLService: CREATE DATABASE %s (id=%lu) via PALF CLOG, lsn=%lu",
           db_name, database_id, lsn.val_);
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

  palf::LSN lsn;
  int64_t scn = 0;
  int ret = log_handler_->append(log_buf, buf_len,
      logservice::DDL_LOG_BASE_TYPE, lsn, scn);
  std::free(log_buf);
  if (ret != 0) return ret;

  return ddl_operator_.create_table(table_schema, table_id);
}

int ObDDLService::recover_schema()
{
  if (!is_inited_ || log_service_ == nullptr) return 0;

  // PALF replay: read all committed log entries for DDL LS
  int ret = log_service_->replay_ls(DDL_LS_ID);
  if (ret != 0) {
    LOG_WARN("ObDDLService: PALF replay returned %d", ret);
    return 0;
  }

  // === SLOG replay (recover metadata state) ===
  slog_logger_.replay(this, [](void *ctx, const storage::ObStorageLogEntry *entry) -> int {
    auto *self = static_cast<ObDDLService *>(ctx);
    if (entry->log_type_ == static_cast<int32_t>(storage::ObStorageLogType::SLOG_CREATE_DATABASE)) {
      std::string db_name(entry->data_, entry->data_len_);
      uint64_t db_id = 0;
      self->ddl_operator_.create_database(db_name.c_str(), db_id);
    }
    return 0;
  });

  LOG_INFO("ObDDLService: PALF CLOG + SLOG replay completed");
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

  if (type == 1) {         // CREATE DATABASE
    uint64_t new_id = 0;
    ddl_operator_.create_database(name, new_id);
    LOG_INFO("CLOG replay: CREATE DATABASE %s (id=%lu, lsn=%lu)", name, new_id, lsn.val_);
  } else if (type == 3) {  // CREATE TABLE
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

}  // namespace rootserver
}  // namespace oceanbase
