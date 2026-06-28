/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/storage/ob_storage_engine.h */

#pragma once

#include <string>
#include <unordered_map>
#include <mutex>

#include "storage/tablet/ob_tablet.h"

namespace oceanbase {
namespace storage {

/**
 * ObStore — top-level engine entry point.
 * Simplified from OB 4.4.2 ObStorageEngine.
 * Manages tablets (one per user table).
 */
class ObStore
{
public:
  ObStore();
  ~ObStore();

  int init(const std::string &base_dir);

  /** Create a new tablet (table). */
  int create_tablet(const std::string &table_name, int64_t tablet_id);

  /** Get a tablet by name. Returns nullptr if not found. */
  ObTablet *get_tablet(const std::string &table_name);

  /** Get a tablet by id. Returns nullptr if not found. */
  ObTablet *get_tablet(int64_t tablet_id);

  /** Drop a tablet. */
  int drop_tablet(const std::string &table_name);

  /** Check freeze for all tablets. */
  void try_freeze_all();

  /** Get base directory. */
  const std::string &get_base_dir() const { return base_dir_; }

private:
  std::string                              base_dir_;
  std::unordered_map<std::string, ObTablet *> tablets_by_name_;
  std::unordered_map<int64_t, ObTablet *>     tablets_by_id_;
  std::mutex                               mutex_;
  bool                                     is_inited_;
};

}  // namespace storage
}  // namespace oceanbase
