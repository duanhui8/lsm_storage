/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/sql/executor/ */

#pragma once
#include "common/sys/rc.h"
#include "rootserver/ob_ddl_service.h"

namespace oceanbase { namespace sql {

class ObCreateDatabaseExecutor {
public:
  ObCreateDatabaseExecutor() = default;
  ~ObCreateDatabaseExecutor() = default;
  int execute(const char *db_name, uint64_t &database_id);
private:
  rootserver::ObDDLService ddl_service_;
};

}}
