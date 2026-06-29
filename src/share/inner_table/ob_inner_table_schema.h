/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase/src/share/inner_table/ob_inner_table_schema.h:342-3660
           /opt/oceanbase/src/share/inner_table/ob_inner_table_schema.{编号}.cpp (按 table_id 分段的实现文件)

 * ============================================================================
 * ObInnerTableSchema — 内部表 Schema 定义（对应 OB 4.4.2 同名类）
 *
 * OB 4.4.2 机制:
 *   所有系统表(__all_database, __all_table, __all_column 等 60+)的 schema 都通过
 *   本类的 static 方法定义。每个方法签名:
 *     static int all_xxx_schema(ObTableSchema &table_schema);
 *
 *   这些方法指针被组织成 creator 数组:
 *     all_core_table_schema_creator[] — 仅 __all_core_table (启动时最先创建)
 *     core_table_schema_creators[]    — __all_table, __all_column, __all_ddl_operation
 *     sys_table_schema_creators[]     — __all_database 等所有 sys 表
 *
 *   Bootstrap 时 (ob_bootstrap.cpp:1042):
 *     for each creator in arrays:
 *       → creator(table_schema)  获取 schema 定义
 *       → prepare_create_partition()  在 sys LS 创建 Tablet 分区
 *       → table_creator.execute()  执行创建
 *
 * MiniOB 实现:
 *   5 个核心系统表的 schema 定义 + 2 个 creator 数组。
 *   在 ObDDLService::init() 中遍历数组注册到 ObSchemaService。
 *
 * schema_create_func 类型 (OB 4.4.2: ob_inner_table_schema.h:3663):
 *   typedef int (*schema_create_func)(ObTableSchema &);
 * ============================================================================
 */
#pragma once

#include "share/schema/ob_table_schema.h"
#include "share/schema/ob_column_schema.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"

namespace oceanbase {
namespace share {

/**
 * ObInnerTableSchema — inner table schema definitions.
 * Each static method defines one system table.
 * From OB 4.4.2 ob_inner_table_schema.h:342-3660.
 */
class ObInnerTableSchema {
public:
  ObInnerTableSchema() = delete;

  static int all_core_table_schema(schema::ObTableSchema &table_schema);
  static int all_table_schema(schema::ObTableSchema &table_schema);
  static int all_column_schema(schema::ObTableSchema &table_schema);
  static int all_ddl_operation_schema(schema::ObTableSchema &table_schema);
  static int all_database_schema(schema::ObTableSchema &table_schema);
};

// OB 4.4.2 ob_inner_table_schema.h:3663
typedef int (*schema_create_func)(schema::ObTableSchema &table_schema);

// OB 4.4.2 ob_inner_table_schema.h:3669
const schema_create_func core_table_schema_creators[] = {
  ObInnerTableSchema::all_table_schema,
  ObInnerTableSchema::all_column_schema,
  ObInnerTableSchema::all_ddl_operation_schema,
  NULL,
};

// OB 4.4.2 ob_inner_table_schema.h:3675
const schema_create_func sys_table_schema_creators[] = {
  ObInnerTableSchema::all_database_schema,
  NULL,
};

}  // namespace share
}  // namespace oceanbase
