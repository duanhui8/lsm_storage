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
// Created by Wangyunlai on 2022/12/30.
//

/**
 * ==========================================================================
 * ★★★ PredicatePushdownRewriter — 谓词下推 ★★★
 * ==========================================================================
 *
 * ★ 谓词下推是什么？
 *   把 WHERE 条件尽量推到靠近数据源的位置（TableGet），
 *   这样可以在扫描表时就过滤掉不符合条件的行，
 *   减少中间结果集的大小。
 *
 * ★ 示例：
 *   SELECT * FROM t1 JOIN t2 ON t1.a = t2.b WHERE t1.x > 10
 *
 *   初始计划：                   下推后：
 *   Filter(t1.x > 10)           Join
 *       |                       /    \
 *     Join             Filter(t1.x>10)  Scan(t2)
 *    /    \                  |
 * Scan(t1) Scan(t2)       Scan(t1)
 *
 *   第1次扫描 t1 时就过滤 x>10，Join 的输入集变小 → 更快。
 *
 * ★ 当前支持的"能下推"条件：
 *   - AND 连接的条件：拆分后逐一检查，能把 Comparison 下推的都下推
 *   - Comparison（a > 10、b = 'hello' 等）：直接下推到 TableGet
 *
 * ★ 不支持的情况（留到 Filter 层执行）：
 *   - OR 条件（太复杂，不知道下推到哪个表）
 *   - 涉及多个表的条件（如 t1.a = t2.b，两个表都有份）
 *
 * ★ 下推后的"残余"处理：
 *   如果所有条件都被下推了，Filter 节点就变空了。
 *   此时插入一个恒真条件 (TRUE)，避免空节点。
 *
 * 💡 提问：为什么关联条件（t1.a = t2.b）不应该被下推？
 *   （提示：下推的前提是"条件只涉及一个表"。t1.a = t2.b 涉及两个表，
 *          下推到任何一个表都没用——另一个表的列在扫描时还不知道值）
 * ==========================================================================
 */

#include "sql/optimizer/predicate_pushdown_rewriter.h"
#include "common/log/log.h"
#include "sql/expr/expression.h"
#include "sql/operator/logical_operator.h"
#include "sql/operator/table_get_logical_operator.h"

/**
 * ★ rewrite — 对 PREDICATE 节点做下推
 *
 * 只在当前节点是 PREDICATE 且子节点是 TABLE_GET 时执行下推。
 * 其他类型的节点直接返回（不做任何修改）。
 */
RC PredicatePushdownRewriter::rewrite(unique_ptr<LogicalOperator> &oper, bool &change_made)
{
  RC rc = RC::SUCCESS;
  if (oper->type() != LogicalOperatorType::PREDICATE) {
    return rc;  // ★ 不是 Predicate 节点，不处理
  }

  if (oper->children().size() != 1) {
    return rc;
  }

  unique_ptr<LogicalOperator> &child_oper = oper->children().front();
  if (child_oper->type() != LogicalOperatorType::TABLE_GET) {
    return rc;  // ★ 子节点不是 TableGet，不能下推（如子节点是 Join）
  }

  // auto table_get_oper = static_cast<TableGetLogicalOperator *>(child_oper.get());
  (void)child_oper;
  vector<unique_ptr<Expression>> &predicate_oper_exprs = oper->expressions();
  if (predicate_oper_exprs.size() != 1) {
    return rc;
  }

  unique_ptr<Expression>             &predicate_expr = predicate_oper_exprs.front();
  vector<unique_ptr<Expression>> pushdown_exprs;

  // ★ 核心：拆分表达式，找出能下推的部分
  rc = get_exprs_can_pushdown(predicate_expr, pushdown_exprs);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to get exprs can pushdown. rc=%s", strrc(rc));
    return rc;
  }

  if (!predicate_expr || is_empty_predicate(predicate_expr)) {
    // ★ 所有条件都下推了，Filter 节点变空
    // 插入恒真条件 (TRUE) 占位
    LOG_TRACE("all expressions of predicate operator were pushdown to table get operator, then make a fake one");
    Value value((bool)true);
    predicate_expr = unique_ptr<Expression>(new ValueExpr(value));
  }

  if (!pushdown_exprs.empty()) {
    change_made = true;
    // table_get_oper->set_predicates(std::move(pushdown_exprs));  // Not yet implemented
  }
  return rc;
}

/**
 * ★ is_empty_predicate — 判断表达式是否为空（所有子表达式都下推了）
 */
bool PredicatePushdownRewriter::is_empty_predicate(unique_ptr<Expression> &expr)
{
  bool bool_ret = false;
  if (!expr) {
    return true;
  }

  if (expr->type() == ExprType::CONJUNCTION) {
    ConjunctionExpr *conjunction_expr = static_cast<ConjunctionExpr *>(expr.get());
    if (conjunction_expr->children().empty()) {
      bool_ret = true;  // ★ AND 节点但没有任何子条件 = 空
    }
  }

  return bool_ret;
}

/**
 * ★★★ get_exprs_can_pushdown — 递归拆分表达式，找出可下推的部分 ★★★
 *
 * 递归逻辑：
 *   - AND 节点：对每个子表达式递归调用。如果某子表达式被下推了，
 *     就从 AND 的 children 中移除它（erase）
 *   - COMPARISON 节点：直接下推（只要条件只涉及当前表的列）
 *   - OR 节点：不处理（太复杂，return UNIMPLEMENTED）
 *
 * 注意 COMPARISON 下推时用 std::move(expr)：
 *   一旦表达式被下推到 pushdown_exprs，原 expr 变成空指针（nullptr），
 *   调用方看到 nullptr 就知道这个条件已经被消费了。
 */
RC PredicatePushdownRewriter::get_exprs_can_pushdown(
    unique_ptr<Expression> &expr, vector<unique_ptr<Expression>> &pushdown_exprs)
{
  RC rc = RC::SUCCESS;
  if (expr->type() == ExprType::CONJUNCTION) {
    ConjunctionExpr *conjunction_expr = static_cast<ConjunctionExpr *>(expr.get());
    if (conjunction_expr->conjunction_type() == ConjunctionExpr::Type::OR) {
      // ★ OR 条件不能拆分下推（没法决定推到哪个表）
      LOG_WARN("unsupported or operation");
      rc = RC::UNIMPLEMENTED;
      return rc;
    }

    // ★ AND 条件：递归检查每个子条件
    vector<unique_ptr<Expression>> &child_exprs = conjunction_expr->children();
    for (auto iter = child_exprs.begin(); iter != child_exprs.end();) {
      rc = get_exprs_can_pushdown(*iter, pushdown_exprs);
      if (rc != RC::SUCCESS) {
        LOG_WARN("failed to get pushdown expressions. rc=%s", strrc(rc));
        return rc;
      }

      if (!*iter) {
        // ★ 子表达式被下推了（move后变空），从 AND 中移除
        iter = child_exprs.erase(iter);
      } else {
        ++iter;
      }
    }
  } else if (expr->type() == ExprType::COMPARISON) {
    // ★ COMPARISON 条件可以直接下推到 TableGet
    // std::move(expr) 后，expr 变为 nullptr
    pushdown_exprs.emplace_back(std::move(expr));
  }
  return rc;
}
