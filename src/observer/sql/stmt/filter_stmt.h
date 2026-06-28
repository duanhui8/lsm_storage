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
 * ★★★【核心学习文件】filter_stmt.h — WHERE 子句的语义表达 ★★★
 * ==========================================================================
 *
 * ★ 这个文件定义了"过滤条件"在语义层面的表示。
 *
 * 三层抽象：
 *   ConditionSqlNode  — 语法层：列名字符串 + 操作符 + 值
 *   FilterUnit        — 语义层：Field 对象（已绑定）+ 操作符 + 值
 *   FilterStmt        — 容器：多个 FilterUnit 的 AND 组合
 *
 * ★ FilterObj — "绑定完成的值引用"
 *
 *   这是 ConditionSqlNode 中 left_is_attr/right_is_attr 的进化版：
 *   ConditionSqlNode 用 int 标记 + 两套字段来表示"列还是值"，
 *   FilterObj 用 is_attr bool + Field + Value 来表示同样的含义。
 *
 *   区别：
 *     ConditionSqlNode.left_attr 是 RelAttrSqlNode（字符串）
 *     FilterObj.field            是 Field（已绑定到实际 Table + FieldMeta）
 *
 * ★ FilterUnit — 单个比较条件
 *
 *   例：WHERE t1.id > 10
 *     left_ = FilterObj{is_attr=true, field=Field{t1, id_meta}}
 *     comp_ = GREATER_THAN
 *     right_ = FilterObj{is_attr=false, value=Value(10)}
 *
 * ★ FilterStmt — AND 串联的条件组
 *
 *   当前不支持 OR，所有条件默认 AND 连接。
 *   即：WHERE a>10 AND b<20 AND c=30 会生成 3 个 FilterUnit。
 *
 * 💡 提问：为什么 FilterObj 不用 union 而是两个字段（field + value）同时存在？
 *   （提示：union 在 C++ 中对非平凡类型有限制（Field 有构造/析构函数）。
 *          MiniOB 选择用 bool is_attr 标记 + 两个字段并存 — 简单安全。
 *          虽然浪费了内存，但对于教学项目足够了）
 *
 * 💡 提问：如果以后要支持 OR 条件（WHERE a>10 OR b<20），
 *   这个设计需要怎么改？
 *   （提示：FilterStmt 需要支持树形结构（AND/OR 表达式树），
 *          而不是简单的 vector。这需要引入 BooleanExpr 节点）
 * ==========================================================================
 */

#pragma once

#include "common/lang/unordered_map.h"
#include "common/lang/vector.h"
#include "sql/expr/expression.h"
#include "sql/parser/parse_defs.h"
#include "sql/stmt/stmt.h"

class Db;
// class Table; (removed)
// class FieldMeta; (removed)

/**
 * ★ FilterObj — 过滤条件中"左边/右边"的值表示
 *
 * 这是一个 tagged value：要么是列引用，要么是常量值。
 * 与 ConditionSqlNode 不同，这里的 field 已经绑定到实际的表对象。
 */
struct FilterObj
{
  bool  is_attr;
  void *field = nullptr;  // Stub
  Value value;

  void init_attr(void *f)
  {
    is_attr     = true;
    this->field = f;
  }

  void init_value(const Value &value)
  {
    is_attr     = false;
    this->value = value;
  }
};

/**
 * ★ FilterUnit — 一个比较条件（左 op 右）
 *
 * 例：WHERE t1.id > 10
 *   left_  = FilterObj{is_attr=true, field=Field(t1, id)}
 *   comp_  = GREATER_THAN
 *   right_ = FilterObj{is_attr=false, value=10}
 */
class FilterUnit
{
public:
  FilterUnit() = default;
  ~FilterUnit() {}

  void set_comp(CompOp comp) { comp_ = comp; }
  CompOp comp() const { return comp_; }

  void set_left(const FilterObj &obj) { left_ = obj; }
  void set_right(const FilterObj &obj) { right_ = obj; }

  const FilterObj &left() const { return left_; }
  const FilterObj &right() const { return right_; }

private:
  CompOp    comp_ = NO_OP;
  FilterObj left_;
  FilterObj right_;
};

/**
 * ★ FilterStmt — WHERE 子句的完整语义表示
 *
 * 包含多个 FilterUnit，所有 FilterUnit 以 AND 方式连接。
 * filter_units_ 中的指针由 FilterStmt 拥有（在析构函数中 delete）。
 *
 * ★ create() 是静态工厂方法：
 *   1. 遍历 ConditionSqlNode 数组（从 ParsedSqlNode 来）
 *   2. 对每个条件调用 create_filter_unit()
 *   3. create_filter_unit() 负责：字符串列名 → Field 对象绑定
 *   4. 如果某边不是列引用，则直接创建 ValueExpr
 */
class FilterStmt
{
public:
  FilterStmt() = default;
  virtual ~FilterStmt();

public:
  const vector<FilterUnit *> &filter_units() const { return filter_units_; }

public:
  static RC create(Db *db, void *default_table, unordered_map<string, void *> *tables,
      const ConditionSqlNode *conditions, int condition_num, FilterStmt *&stmt);

  static RC create_filter_unit(Db *db, void *default_table, unordered_map<string, void *> *tables,
      const ConditionSqlNode &condition, FilterUnit *&filter_unit);

private:
  vector<FilterUnit *> filter_units_;  // 默认当前都是AND关系
};
