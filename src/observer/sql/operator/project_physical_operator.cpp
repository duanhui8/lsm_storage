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
// Created by WangYunlai on 2022/07/01.
//

/**
 * ==========================================================================
 * ★★★ ProjectPhysicalOperator — 列投影（SELECT 列裁剪）算子 ★★★
 * ==========================================================================
 *
 * ★ 角色：算子树根节点，按 SELECT 列列表裁剪输出。
 *   它是"改变行内容"的算子（TableScan 和 Predicate 都不改变内容）。
 *
 * ★ 算子树中的位置（总是最顶层）：
 *   Project (id, name)               ← 列裁剪：输出 id 和 name
 *       ↑
 *   Predicate (WHERE age > 20)       ← 行过滤
 *       ↑
 *   TableScan (t1)                   ← 全表扫描：输出所有列
 *
 * ★ 为什么 Project 在最顶层？
 *   - 如果 Project 在 Predicate 之前，WHERE 条件中引用的列
 *     可能已经被裁剪掉了（如 SELECT id FROM t1 WHERE age > 20
 *     中的 age 列在 SELECT 中没出现但在 WHERE 中需要）
 *   - 所以 Project 必须是最后一步，在过滤完成后再裁剪
 *
 * ★ next() 极简的原因：
 *   Project 不改变行数，只改变行内容。
 *   next() 直接调用子节点的 next() 来推进迭代位置。
 *   行内容的改写（列裁剪/表达式求值）发生在 current_tuple() 中。
 *
 * ★ next() 和 current_tuple() 的分工：
 *   next()           — 推进迭代器位置（移动游标到下一行）
 *   current_tuple()  — 获取当前位置的数据（创建投影后的 Tuple 视图）
 *   这两个操作可以独立调用，各有各的语义。
 *
 * 💡 提问：如果 children_ 为空（如 CALC 1+2，没有 FROM），next() 直接返回 EOF。
 *   那计算结果在哪里输出？
 *   （提示：CalcPhysicalOperator 的 next() 只返回一次 SUCCESS，
 *          current_tuple() 计算表达式值返回结果。与 Select 不同，
 *          Calc 的表达式中包含具体值，不需要从表中读数据）
 * ==========================================================================
 */

#include "sql/operator/project_physical_operator.h"
#include "common/log/log.h"

using namespace std;

ProjectPhysicalOperator::ProjectPhysicalOperator(vector<unique_ptr<Expression>> &&expressions)
  : expressions_(std::move(expressions)), tuple_(expressions_)
{
}

RC ProjectPhysicalOperator::open(Trx *trx)
{
  if (children_.empty()) {
    return RC::SUCCESS;  // CALC/常量查询：没有子节点，不需要 open
  }

  PhysicalOperator *child = children_[0].get();
  RC                rc    = child->open(trx);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to open child operator: %s", strrc(rc));
    return rc;
  }

  return RC::SUCCESS;
}

/**
 * ★ next — 透传子节点的 next()
 *
 * 极简实现：Project 不改变行数，直接传递迭代信号。
 * 如果子节点返回 SUCCESS（有新行），Project 也返回 SUCCESS。
 * 如果子节点返回 RECORD_EOF，Project 也返回 RECORD_EOF。
 */
RC ProjectPhysicalOperator::next()
{
  if (children_.empty()) {
    return RC::RECORD_EOF;
  }
  return children_[0]->next();
}

RC ProjectPhysicalOperator::close()
{
  if (!children_.empty()) {
    children_[0]->close();
  }
  return RC::SUCCESS;
}

/**
 * ★★★ current_tuple — 列投影的核心 ★★★
 *
 * 这里通过 ExpressionTuple 对 expressions_ 中的每个表达式求值。
 *
 * 例：原始行 {id=1, name='Alice', age=25, city='NYC'}
 *     SELECT id, name    → 投影后 {id=1, name='Alice'}
 *     SELECT id, age+1   → 投影后 {id=1, age+1=26}
 *
 * ExpressionTuple::set_tuple() 设置"输入行"（子节点的 Tuple），
 * 然后对每个 expression_ 分别求值，结果组成"输出行"。
 */
Tuple *ProjectPhysicalOperator::current_tuple()
{
  // SELECT * → no expressions, pass through child's tuple directly
  if (expressions_.empty() && !children_.empty()) {
    return children_[0]->current_tuple();
  }
  tuple_.set_tuple(children_[0]->current_tuple());
  return &tuple_;
}

/**
 * ★ tuple_schema — 定义输出列的 schema
 *
 * 每个表达式对应一列，列名从表达式的 name() 获取。
 * 例如：SELECT id AS my_id, age+1 AS next_age FROM t1
 *   schema = ["my_id | next_age"]
 *
 * 这个 schema 决定了写入客户端时的列头显示。
 */
RC ProjectPhysicalOperator::tuple_schema(TupleSchema &schema) const
{
  // If no expressions (SELECT *), delegate to child operator's schema
  if (expressions_.empty() && !children_.empty()) {
    return children_[0]->tuple_schema(schema);
  }
  for (const unique_ptr<Expression> &expression : expressions_) {
    schema.append_cell(expression->name());
  }
  return RC::SUCCESS;
}
