/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/share/schema/ob_ddl_sql_service.h */

#include "share/schema/ob_ddl_sql_service.h"
#include "storage/tablet/ob_tablet.h"
#include "storage/tablet/ob_tablet_table_store.h"
#include "storage/memtable/ob_memtable.h"
#include "common/log/log.h"
#include <cstring>
#include <cstdlib>

namespace oceanbase {
namespace share {
namespace schema {

int ObDDLSqlService::log_operation(ObDDLOperationType op_type, const char *db_name)
{
  if (system_tablets_ == nullptr) { LOG_WARN("tablet map null"); return -1; }
  auto it = system_tablets_->find("__all_ddl_operation");
  if (it == system_tablets_->end()) { LOG_WARN("__all_ddl_operation tablet not found"); return -1; }
  auto *tablet = static_cast<storage::ObTablet *>(it->second);
  auto *mt = tablet->get_table_store()->get_active_memtable();
  if (mt == nullptr) return -1;

  // Build row: [operation_type 8B][schema_version 8B][db_name_len 4B][db_name]
  // Use a synthetic key = (op_type << 56) | (schema_version << 48) | timestamp for uniqueness
  uint64_t op_type_val = static_cast<uint64_t>(op_type);
  uint64_t schema_version = 1;
  uint64_t timestamp = 0; // simplified
  uint64_t dd_key = (op_type_val << 56) | (schema_version << 48) | (timestamp & 0xFFFFFFFFFFFF);

  int64_t name_len = strlen(db_name);
  int64_t row_size = sizeof(int64_t) + sizeof(int64_t) + sizeof(int32_t) + name_len;
  char *row_buf = static_cast<char *>(std::malloc(row_size));
  char *rp = row_buf;
  std::memcpy(rp, &op_type_val, sizeof(int64_t)); rp += sizeof(int64_t);
  std::memcpy(rp, &schema_version, sizeof(int64_t)); rp += sizeof(int64_t);
  int32_t len32 = static_cast<int32_t>(name_len);
  std::memcpy(rp, &len32, sizeof(int32_t)); rp += sizeof(int32_t);
  std::memcpy(rp, db_name, name_len);

  storage::ObStoreCtx ctx; ctx.tx_id_ = 1;
  storage::ObStoreRow row;
  storage::ObStoreRowkey rk(reinterpret_cast<const char *>(&dd_key), sizeof(dd_key));
  row.rowkey_ = rk;
  row.dml_flag_ = blocksstable::ObDmlFlag::DF_INSERT;
  row.row_value_.assign(row_buf, row_buf + row_size);
  std::free(row_buf);

  int ret = mt->set(ctx, row);
  tablet->get_freezer()->try_freeze();
  LOG_INFO("ObDDLSqlService: INSERT INTO __all_ddl_operation (type=%lu, db=%s)",
           op_type_val, db_name);
  return ret;
}

}  // namespace schema
}  // namespace share
}  // namespace oceanbase
