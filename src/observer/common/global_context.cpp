/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2. */

#include "common/global_context.h"

static GlobalContext global_context;
GlobalContext &GlobalContext::instance() { return global_context; }
