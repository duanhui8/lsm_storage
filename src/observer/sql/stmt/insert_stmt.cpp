/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Wangyunlai on 2022/5/22.
//

/**
 * ==========================================================================
 * ★ InsertStmt::create — INSERT 语义解析
 * ==========================================================================
 *
 * ★ 从 InsertSqlNode 构建 InsertStmt 的过程：
 *   1. 查找目标表（table_name → Table*）
 *   2. 验证值的数量与表的列数匹配
 *   3. 创建 InsertStmt 对象
 *
 * ★ 验证逻辑：
 *   field_num = table_meta.field_num() - table_meta.sys_field_num()
 *   - 总列数减去系统列数（_null, _rid 等隐藏列）
 *   - 确保提供的值数量 = 用户定义的列数
 *   - 不支持指定列名（INSERT INTO t1(id, name) VALUES(...)）→ 缺少的列必须给值
 *
 * ★ 值的存储方式：
 *   values_ 是一个指向原始 Value 数组的指针（不拷贝数据）。
 *   这意味着 InsertSqlNode 的生命周期必须长于 InsertStmt。
 *   在 MiniOB 中，ParsedSqlNode 在整个 Stmt 生命周期中都存活，所以安全。
 *
 * 💡 提问：为什么不支持 INSERT INTO t1(id, name) VALUES (1, 'hello')？
 *   （提示：这需要解析"列名列表"（id, name），并验证列名存在且不重复。
 *          MiniOB 选择了简化：按表定义顺序提供所有列的值。
 *          加上这个功能需要修改 yacc_sql.y 的语法规则和 InsertStmt::create）
 * ==========================================================================
 */

#include "sql/stmt/insert_stmt.h"
#include "common/log/log.h"

InsertStmt::InsertStmt(Table *table, const Value *values, int value_amount)
    : table_(table), values_(values), value_amount_(value_amount)
{}

RC InsertStmt::create(Db *db, const InsertSqlNode &inserts, Stmt *&stmt)
{
  const char *table_name = inserts.relation_name.c_str();
  if (nullptr == db || nullptr == table_name || inserts.values.empty()) {
    LOG_WARN("invalid argument. db=%p, table_name=%p, value_num=%d",
        db, table_name, static_cast<int>(inserts.values.size()));
    return RC::INVALID_ARGUMENT;
  }

  // ★ 第1步：查找目标表
  Table *table = db->find_table(table_name);
  if (nullptr == table) {
    LOG_WARN("no such table. db=%s, table_name=%s", db->name(), table_name);
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  // ★ 第2步：验证值的数量与表的列数匹配
  const Value     *values     = inserts.values.data();
  const int        value_num  = static_cast<int>(inserts.values.size());
  const TableMeta &table_meta = table->table_meta();
  const int        field_num  = table_meta.field_num() - table_meta.sys_field_num();
  if (field_num != value_num) {
    LOG_WARN("schema mismatch. value num=%d, field num in schema=%d", value_num, field_num);
    return RC::SCHEMA_FIELD_MISSING;
  }

  // ★ 第3步：创建 InsertStmt
  stmt = new InsertStmt(table, values, value_num);
  return RC::SUCCESS;
}
