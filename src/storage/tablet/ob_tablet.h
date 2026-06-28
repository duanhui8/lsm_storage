/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/tablet/tablet/ob_tablet.h */

#pragma once

#include <string>

#include "storage/tablet/ob_tablet_table_store.h"
#include "storage/ls/ob_freezer.h"
#include "storage/logservice/ob_log_handler.h"

namespace oceanbase {
namespace logservice {
class ObLogService;
}

namespace storage {

/**
 * ObTablet — logical storage unit (one per table).
 * Simplified from OB 4.4.2 ObTablet.
 * Owns ObTableStore + ObFreezer + ObLogHandler.
 */
class ObTablet
{
public:
  ObTablet();
  ~ObTablet();

  int init(int64_t tablet_id, logservice::ObLogService *log_service = nullptr);

  ObTableStore  *get_table_store() { return &table_store_; }
  ObFreezer     *get_freezer() { return &freezer_; }
  logservice::ObLogHandler *get_log_handler() { return log_handler_; }

  int64_t get_tablet_id() const { return tablet_id_; }

  /** Replay committed rows into the memtable on recovery. */
  int recover();

private:
  int64_t                    tablet_id_;
  ObTableStore               table_store_;
  ObFreezer                  freezer_;
  logservice::ObLogHandler  *log_handler_;
  logservice::ObLogService  *log_service_;
  bool                       is_inited_;
};

}  // namespace storage
}  // namespace oceanbase
