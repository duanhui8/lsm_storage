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
// Created by Wangyunlai on 2022/12/13.
//

/**
 * ==========================================================================
 * ★★★ ExpressionRewriter — 表达式级重写（简化）★★★
 * ==========================================================================
 *
 * ★ 作用：
 *   对每个 LogicalOperator 节点中的 Expression 做语法等价简化。
 *   比 PredicatePushdown 更底层——它不改变计划树结构，
 *   只简化表达式本身。
 *
 * ★ 两个子规则：
 *   1. ComparisonSimplificationRule：简化比较表达式
 *      如 a > 3 AND a > 5 → a > 5（取更严格的条件）
 *   2. ConjunctionSimplificationRule：简化合取表达式
 *      如 TRUE AND b < 10 → b < 10（消除恒真条件）
 *
 * ★ 递归策略：
 *   对每个 LogicalOperator 节点：
 *     1. 用所有表达式规则重写当前节点的表达式列表
 *     2. 递归重写孩子节点的表达式
 *
 * ★ rewrite_expression 的递归：
 *   对表达式树做深度遍历，递归处理子表达式。
 *   如 COMPARISON(CAST(a), b)：先重写 CAST 的 child，再重写 right b。
 *
 * 💡 提问：为什么表达式简化要在谓词下推之前做？
 *   （提示：先简化可以消除冗余条件。比如 a>3 AND a>5 先简化为 a>5，
 *          再下推，只需要下推一个条件而不是两个）
 * ==========================================================================
 */

#include "sql/optimizer/expression_rewriter.h"
#include "common/log/log.h"
#include "sql/optimizer/comparison_simplification_rule.h"
#include "sql/optimizer/conjunction_simplification_rule.h"

using namespace std;

/**
 * ★ 构造函数 — 注册表达式级简化规则
 */
ExpressionRewriter::ExpressionRewriter()
{
  expr_rewrite_rules_.emplace_back(new ComparisonSimplificationRule);
  expr_rewrite_rules_.emplace_back(new ConjunctionSimplificationRule);
}

/**
 * ★★★ rewrite — 重写逻辑算子中的所有表达式 ★★★
 *
 * 对当前节点：
 *   遍历所有表达式，对每个表达式调用 rewrite_expression。
 * 然后递归处理所有子节点。
 */
RC ExpressionRewriter::rewrite(unique_ptr<LogicalOperator> &oper, bool &change_made)
{
  RC rc = RC::SUCCESS;

  bool sub_change_made = false;

  // ★ 第1步：重写当前节点的表达式列表
  vector<unique_ptr<Expression>> &expressions = oper->expressions();
  for (unique_ptr<Expression> &expr : expressions) {
    rc = rewrite_expression(expr, sub_change_made);
    if (rc != RC::SUCCESS) {
      break;
    }

    if (sub_change_made && !change_made) {
      change_made = true;
    }
  }

  if (rc != RC::SUCCESS) {
    return rc;
  }

  // ★ 第2步：递归重写子节点的表达式
  vector<unique_ptr<LogicalOperator>> &child_opers = oper->children();
  for (unique_ptr<LogicalOperator> &child_oper : child_opers) {
    bool sub_change_made = false;
    rc                   = rewrite(child_oper, sub_change_made);
    if (sub_change_made && !change_made) {
      change_made = true;
    }
    if (rc != RC::SUCCESS) {
      break;
    }
  }
  return rc;
}

/**
 * ★★★ rewrite_expression — 递归重写单个表达式 ★★★
 *
 * 处理顺序：
 *   1. 对当前表达式应用所有规则（规则可能替换整个表达式）
 *   2. 根据表达式类型递归处理子表达式：
 *      - FIELD / VALUE：叶子节点，不做处理
 *      - CAST：递归处理 cast 的 child
 *      - COMPARISON：递归处理 left 和 right
 *      - CONJUNCTION：递归处理每个 child
 *      - AGGREGATION：递归处理内部表达式
 *
 * 这是一个典型的 Visitor 模式递归实现。
 */
RC ExpressionRewriter::rewrite_expression(unique_ptr<Expression> &expr, bool &change_made)
{
  RC rc = RC::SUCCESS;

  change_made = false;
  // ★ 步骤1: 对当前表达式应用所有规则
  for (unique_ptr<ExpressionRewriteRule> &rule : expr_rewrite_rules_) {
    bool sub_change_made = false;

    rc = rule->rewrite(expr, sub_change_made);
    if (sub_change_made && !change_made) {
      change_made = true;
    }
    if (rc != RC::SUCCESS) {
      break;
    }
  }

  if (change_made || rc != RC::SUCCESS) {
    return rc;
  }

  // ★ 步骤2: 根据表达式类型递归处理子表达式
  switch (expr->type()) {
    case ExprType::FIELD:
    case ExprType::VALUE: {
      // ★ 叶子节点——没有子表达式，什么都不做
    } break;

    case ExprType::CAST: {
      unique_ptr<Expression> &child_expr = (static_cast<CastExpr *>(expr.get()))->child();
      rc = rewrite_expression(child_expr, change_made);
    } break;

    case ExprType::COMPARISON: {
      // ★ 关系表达式（a > 10）：递归处理左右子表达式
      auto comparison_expr = static_cast<ComparisonExpr *>(expr.get());
      unique_ptr<Expression> &left_expr  = comparison_expr->left();
      unique_ptr<Expression> &right_expr = comparison_expr->right();

      bool left_change_made = false;
      rc = rewrite_expression(left_expr, left_change_made);
      if (rc != RC::SUCCESS) {
        return rc;
      }

      bool right_change_made = false;
      rc = rewrite_expression(right_expr, right_change_made);
      if (rc != RC::SUCCESS) {
        return rc;
      }

      if (left_change_made || right_change_made) {
        change_made = true;
      }
    } break;

    case ExprType::CONJUNCTION: {
      // ★ 合取表达式（AND）：递归处理每个子条件
      auto conjunction_expr = static_cast<ConjunctionExpr *>(expr.get());
      vector<unique_ptr<Expression>> &children = conjunction_expr->children();
      for (unique_ptr<Expression> &child_expr : children) {
        bool sub_change_made = false;
        rc = rewrite_expression(child_expr, sub_change_made);
        if (rc != RC::SUCCESS) {
          LOG_WARN("failed to rewriter conjunction sub expression. rc=%s", strrc(rc));
          return rc;
        }

        if (sub_change_made && !change_made) {
          change_made = true;
        }
      }
    } break;

    default: {
      // ★ AGGREGATION / ARITHMETIC 等——不做深度重写
    } break;
  }
  return rc;
}
