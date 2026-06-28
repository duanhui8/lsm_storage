/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/rootserver/ob_ddl_service.h */

#pragma once

#include "common/sys/rc.h"
#include "rootserver/ob_ddl_operator.h"
#include "storage/logservice/ob_log_service.h"
#include "storage/logservice/ob_log_handler.h"
#include <memory>

namespace oceanbase {
namespace rootserver {

/**
 * ObDDLService — DDL orchestrator (matching OB 4.4.2 ob_ddl_service.h).
 * Coordinates: schema operations + CLOG writing via PALF + tablet creation.
 */
class ObDDLService : public logservice::ObIReplaySubHandler {
public:
  ObDDLService() = default;
  ~ObDDLService() = default;

  int init(const char *base_dir = "miniob/store");

  /** create_database — full CREATE DATABASE flow with PALF CLOG */
  int create_database(const char *db_name, uint64_t &database_id);
  int create_table(share::schema::ObTableSchema &table_schema, uint64_t &table_id);

  /** Replay CLOG via PALF on startup to recover schema */
  int recover_schema();

  /** ObIReplaySubHandler — replay CLOG entry into schema */
  int replay(const void *buffer, int64_t nbytes,
             const palf::LSN &lsn, int64_t scn) override;

  /** Get singleton */
  static ObDDLService &instance();

  /** Access the log service for external replay registration */
  logservice::ObLogService *get_log_service() { return log_service_.get(); }

private:
  static constexpr int64_t DDL_LS_ID = 1;    // Log Stream ID for DDL operations

  ObDDLOperator ddl_operator_;
  std::unique_ptr<logservice::ObLogService> log_service_;
  logservice::ObLogHandler *log_handler_ = nullptr;
  std::string base_dir_;
  bool is_inited_ = false;
};

}  // namespace rootserver
}  // namespace oceanbase
