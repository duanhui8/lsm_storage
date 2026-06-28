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
// Created by Wangyunlai on 2022/6/5.
//

/**
 * ==========================================================================
 * ★★★【核心学习文件】select_stmt.h — SELECT 语句的语义对象 ★★★
 * ==========================================================================
 *
 * ★ SelectStmt 是 ResolveStage 对 SELECT 语句进行语义解析后的产物。
 *
 * 转换过程（SelectStmt::create）:
 *   1. 遍历 relations_ 中的每个表名字符串 → 从 Db 查找 Table* 对象
 *   2. 遍历 conditions_ 中的每个条件 → 创建 FilterStmt（FilterObj 绑定到 Field*）
 *   3. 遍历 expressions_ → 解析 UnboundFieldExpr → 绑定到具体的 Field
 *
 * 例：SELECT t1.id, t1.name FROM t1 WHERE t1.id > 10
 *
 *   输入（SelectSqlNode）:
 *     relations = ["t1"]  ← 还是字符串
 *     conditions = [{left=t1.id, GT, right=10}]
 *     expressions = [UnboundFieldExpr("t1.id"), UnboundFieldExpr("t1.name")]
 *
 *   输出（SelectStmt）:
 *     tables_ = [Table*]  ← 已是 Table 对象指针
 *     filter_stmt_ = FilterStmt {
 *       filter_units = [FilterUnit{left=Field(t1.id), GT, right=Value(10)}]
 *     }
 *     query_expressions_ = [FieldExpr(Field{t1.id}), FieldExpr(Field{t1.name})]
 *
 * ★ SelectStmt 是唯一有 FilterStmt 和 GroupBy 的 Stmt 子类。
 *   其他 DML 语句（INSERT/DELETE/UPDATE）只涉及单表和简单的条件过滤。
 *
 * 💡 提问：为什么 tables_ 用 vector<Table*> 而不是单个 Table*？
 *   （提示：SELECT 支持多表查询（JOIN）。虽然 MiniOB 的 JOIN 语法受限，
 *          但 FROM t1, t2 已经隐式产生笛卡尔积或等值连接）
 */
#pragma once

#include "common/sys/rc.h"
#include "sql/stmt/stmt.h"
#include "sql/expr/expression.h"

class FilterStmt;
class Db;

class SelectStmt : public Stmt
{
public:
  SelectStmt() = default;
  ~SelectStmt() override;

  StmtType type() const override { return StmtType::SELECT; }

public:
  /**
   * ★ SelectStmt::create — 从 SelectSqlNode 构建 SelectStmt
   *
   * 这是 ResolveStage 调用的静态工厂方法。
   * 核心工作：
   *   1. 查找所有引用的表（字符串 → Table*）
   *   2. 创建 FilterStmt（条件表达式绑定到 Field）
   *   3. 解析查询表达式（UnboundFieldExpr → FieldExpr）
   */
  static RC create(Db *db, SelectSqlNode &select_sql, Stmt *&stmt);

public:
  const vector<void *> &tables() const { return tables_; }
  FilterStmt            *filter_stmt() const { return filter_stmt_; }

  /**
   * ★ query_expressions() — SELECT 后面的表达式列表
   *
   * 返回的是 movable 引用，用于转移所有权到 ProjectLogicalOperator。
   * 调用后 SelectStmt 中的 expressions 被清空（unique_ptr move 语义）。
   *
   * 这体现了"数据传递"模式：语义分析产物传递给优化器，
   * 转移所有权避免了拷贝。
   */
  vector<unique_ptr<Expression>> &query_expressions() { return query_expressions_; }
  vector<unique_ptr<Expression>> &group_by() { return group_by_; }

private:
  vector<unique_ptr<Expression>> query_expressions_;  ///< SELECT 列表达式
  vector<void *>                tables_;              ///< FROM table pointers
  FilterStmt                    *filter_stmt_ = nullptr; ///< WHERE 过滤条件
  vector<unique_ptr<Expression>> group_by_;            ///< GROUP BY 表达式
};
