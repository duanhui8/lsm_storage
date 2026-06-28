/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#pragma once
#include "common/sys/rc.h"
class SQLStageEvent;
class AnalyzeTableExecutor {
public:
  AnalyzeTableExecutor() = default;
  ~AnalyzeTableExecutor() = default;
  RC execute(SQLStageEvent *) { return RC::UNIMPLEMENTED; }
};
