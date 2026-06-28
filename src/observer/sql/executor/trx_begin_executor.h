/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#pragma once
#include "common/sys/rc.h"
#include "event/session_event.h"
#include "event/sql_event.h"

class TrxBeginExecutor {
public:
  TrxBeginExecutor() = default;
  virtual ~TrxBeginExecutor() = default;
  RC execute(SQLStageEvent *) { return RC::SUCCESS; }
};
