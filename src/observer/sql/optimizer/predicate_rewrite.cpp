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
// Created by Wangyunlai on 2022/12/29.
//

/**
 * ==========================================================================
 * ★★★ PredicateRewriteRule — 谓词节点消除 ★★★
 * ==========================================================================
 *
 * ★ 作用：消除无用的 PREDICATE 节点
 *
 * 当 PREDICATE 节点的子条件被简化成常量 TRUE/FALSE 后，
 * 需要从计划树中消除这个冗余节点：
 *
 *   - 恒 TRUE：删除 Filter 节点，把它的孩子（孙子）直接接到爷爷上
 *        GrandParent
 *            │
 *        [Filter(TRUE)]   →    GrandParent
 *            │                    │
 *        [TableGet]          [TableGet]
 *
 *   - 恒 FALSE：删除 Filter 节点及所有子树（永远不会返回任何行）
 *
 * ★ 触发条件（所有条件都满足时）：
 *   1. cur 恰好有 1 个子节点
 *   2. 子节点是 PREDICATE 类型
 *   3. 子节点恰好有 1 个表达式
 *   4. 该表达式是 VALUE(TRUE/FALSE) 类型
 *
 * ★ 和 ConjunctionSimplificationRule 的配合：
 *   ConjunctionSimplification 把 "AND(TRUE, a>3)" 简化为 "TRUE"，
 *   然后 PredicateRewrite 看到 Filter(TRUE) 并消除它。
 *   两条规则各司其职——前者做表达式级简化，后者做计划树结构优化。
 *
 * 💡 提问：恒 FALSE 时为什么 child_opers.clear() 就足够了？
 *   （提示：PREDICATE(FALSE) 表示"过滤全部行"，所以它的子树（TableGet等）
 *          永远不会被访问到。清除子节点只是标记"这个分支没有数据"，
 *          上层算子看到空子节点就知道结果是空集）
 * ==========================================================================
 */

#include "sql/optimizer/predicate_rewrite.h"
#include "sql/operator/logical_operator.h"

RC PredicateRewriteRule::rewrite(unique_ptr<LogicalOperator> &oper, bool &change_made)
{
  vector<unique_ptr<LogicalOperator>> &child_opers = oper->children();
  if (child_opers.size() != 1) {
    return RC::SUCCESS;  // ★ 当前节点不是单子节点结构，不处理
  }

  auto &child_oper = child_opers.front();
  if (child_oper->type() != LogicalOperatorType::PREDICATE) {
    return RC::SUCCESS;  // ★ 子节点不是 PREDICATE
  }

  vector<unique_ptr<Expression>> &expressions = child_oper->expressions();
  if (expressions.size() != 1) {
    return RC::SUCCESS;
  }

  unique_ptr<Expression> &expr = expressions.front();
  if (expr->type() != ExprType::VALUE) {
    return RC::SUCCESS;  // ★ 表达式不是常量值
  }

  // ★ 核心逻辑：根据 TRUE 还是 FALSE 做不同处理
  auto value_expr = static_cast<ValueExpr *>(expr.get());
  bool bool_value = value_expr->get_value().get_boolean();
  if (true == bool_value) {
    // ★ 恒 TRUE：删除 Filter，把孙子节点提升为儿子
    //      Parent                  Parent
    //        │                       │
    //    Filter(TRUE)     →    TableGet（原孙子）
    //        │
    //    TableGet
    vector<unique_ptr<LogicalOperator>> grand_child_opers;
    grand_child_opers.swap(child_oper->children());  // ★ 接管孙子节点
    child_opers.clear();                              // ★ 删除 Filter
    for (auto &grand_child_oper : grand_child_opers) {
      oper->add_child(std::move(grand_child_oper));   // ★ 孙子提升为儿子
    }
  } else {
    // ★ 恒 FALSE：整个分支无结果，清空子节点
    child_opers.clear();
  }

  change_made = true;
  return RC::SUCCESS;
}
