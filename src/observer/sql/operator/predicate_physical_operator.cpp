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
// Created by WangYunlai on 2022/6/27.
//

/**
 * ==========================================================================
 * ★★★ PredicatePhysicalOperator — WHERE 条件过滤算子 ★★★
 * ==========================================================================
 *
 * ★ 角色：算子树中间节点，按 WHERE 条件过滤行。
 *   只有满足条件的行能"通过"，不满足的跳过。
 *
 * ★ 火山模型体现：
 *   next() {
 *     while (child->next() == SUCCESS) {    // 从子节点拿下一行
 *       if (expression_->evaluate(tuple) == true)  // 检查是否满足条件
 *         return SUCCESS;                   // 满足 → 返回这一行
 *       // 不满足 → 继续循环（跳过这行）
 *     }
 *     return RECORD_EOF;  // 子节点数据耗尽
 *   }
 *
 * ★ 关键：Predicate 的输出行数 ≤ 输入行数
 *   它只做过滤，不改变行内容，也不产生新行。
 *   所以 current_tuple() 直接透传子节点的 Tuple。
 *
 * 💡 提问：如果 WHERE 条件是 "true"（永远为真），
 *   这个 while 循环的性能如何？有什么优化空间？
 *   （提示：优化器可以在优化阶段识别永真条件，
 *          直接跳过 Predicate 算子，让 Project 直接连到 TableScan。
 *          这叫做"谓词消除"优化）
 * ==========================================================================
 */

#include "sql/operator/predicate_physical_operator.h"
#include "common/log/log.h"
#include "sql/stmt/filter_stmt.h"

/**
 * ★ 构造函数 — 确保表达式求值是布尔类型
 *
 * expression_ 是一个表达式树，求值结果必须是 BOOLEAN。
 * 例如 WHERE id > 10 AND name = 'Alice' 对应的 expression 是：
 *   ConjunctionExpr(AND)
 *     └── ComparisonExpr(GT, FieldExpr(id), ValueExpr(10))
 *     └── ComparisonExpr(EQ, FieldExpr(name), ValueExpr('Alice'))
 *
 * ASSERT 确保类型正确 — 如果优化器传了非布尔表达式，说明出 bug 了。
 */
PredicatePhysicalOperator::PredicatePhysicalOperator(std::unique_ptr<Expression> expr) : expression_(std::move(expr))
{
  ASSERT(expression_->value_type() == AttrType::BOOLEANS, "predicate's expression should be BOOLEAN type");
}

/**
 * ★ open — 初始化子节点
 *
 * Predicate 严格只有一个子节点（单输入算子）。
 * 如果 children_ 数量不是 1，说明优化器生成计划时出错。
 */
RC PredicatePhysicalOperator::open(Trx *trx)
{
  if (children_.size() != 1) {
    LOG_WARN("predicate operator must has one child");
    return RC::INTERNAL;
  }

  return children_[0]->open(trx);
}

/**
 * ★★★ next — 逐行过滤（火山模型核心） ★★★
 *
 * 逻辑：
 *   while (child->next()) {
 *     tuple = child->current_tuple();
 *     expression_->get_value(tuple, value);  // 计算 WHERE 条件
 *     if (value.get_boolean()) return SUCCESS; // 满足 → 返回
 *     // 不满足 → 继续循环
 *   }
 *   return RECORD_EOF;  // 子节点没数据了
 *
 * ★ 与 TableScan::next() 的 while 循环双剑合璧：
 *   TableScan 跳过不满足"下推谓词"的行
 *   Predicate 跳过不满足"剩余谓词"的行
 *   两层过滤各司其职，减少不必要的数据传输和表达式求值。
 */
RC PredicatePhysicalOperator::next()
{
  RC                rc   = RC::SUCCESS;
  PhysicalOperator *oper = children_.front().get();

  while (RC::SUCCESS == (rc = oper->next())) {
    Tuple *tuple = oper->current_tuple();
    if (nullptr == tuple) {
      rc = RC::INTERNAL;
      LOG_WARN("failed to get tuple from operator");
      break;
    }

    Value value;
    rc = expression_->get_value(*tuple, value);
    if (rc != RC::SUCCESS) {
      return rc;
    }

    if (value.get_boolean()) {
      return rc;  // ★ 满足条件，返回这行
    }
  }
  return rc;  // RC::RECORD_EOF 或其他错误
}

RC PredicatePhysicalOperator::close()
{
  children_[0]->close();
  return RC::SUCCESS;
}

/**
 * ★ current_tuple — 透传给子节点
 *
 * Predicate 不修改数据内容，只决定"哪些行能通过"。
 * 所以当前 Tuple 就是子节点的 current_tuple。
 */
Tuple *PredicatePhysicalOperator::current_tuple() { return children_[0]->current_tuple(); }

/**
 * ★ tuple_schema — 透传 schema
 *
 * Predicate 不改变输出列的结构，schema 与子节点完全一致。
 * 也就是说，经过 Predicate 过滤后，输出列名和类型不变。
 */
RC PredicatePhysicalOperator::tuple_schema(TupleSchema &schema) const
{
  return children_[0]->tuple_schema(schema);
}
