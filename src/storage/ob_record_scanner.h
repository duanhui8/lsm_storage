/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
ObRecordScanner — reads rows from ObTableStore. */

#pragma once

#include <vector>
#include "common/sys/rc.h"
#include "tablet/ob_tablet_table_store.h"

class ObRecordScanner
{
public:
  ObRecordScanner(oceanbase::storage::ObTableStore *table_store);
  ~ObRecordScanner() = default;

  RC open_scan();
  RC close_scan();
  RC next_record(std::vector<char> &row_data);

private:
  oceanbase::storage::ObTableStore *table_store_;
  std::vector<std::vector<char>> row_data_cache_;
  int current_idx_ = -1;
  bool is_open_ = false;
};
