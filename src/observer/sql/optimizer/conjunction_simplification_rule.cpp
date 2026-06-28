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
// Created by Wangyunlai on 2022/12/26.
//

/**
 * ==========================================================================
 * ★★★ ConjunctionSimplificationRule — 合取表达式简化 ★★★
 * ==========================================================================
 *
 * ★ 作用：基于布尔代数简化 AND / OR 表达式
 *
 * ★ 简化规则（AND）：
 *   1. 消除恒真条件：  a > 3 AND TRUE  →  a > 3
 *   2. 恒假短路：      a > 3 AND FALSE →  FALSE（整个表达式恒假）
 *   3. 单子表达式折叠：只剩一个子条件时，直接用子条件替代整个 AND 节点
 *
 * ★ 简化规则（OR）：
 *   1. 消除恒假条件：  a > 3 OR FALSE  →  a > 3
 *   2. 恒真短路：      a > 3 OR TRUE  →  TRUE（整个表达式恒真）
 *   3. 单子表达式折叠：同 AND
 *
 * ★ 这些简化在什么场景下触发？
 *   比如 PredicatePushdown 下推后，Filter 中只剩恒 TRUE 占位符，
 *   本规则配合 PredicateRewriteRule 一起消除这些无用的条件节点。
 *
 * 💡 提问：为什么代码中 child_exprs.erase(iter) 后不需要 ++iter？
 *   （提示：erase 返回被删除元素之后的有效迭代器。
 *          但这里没有用 erase 的返回值，而是 erase 后进入下一轮循环。
 *          由于 erase 使 iter 失效，下一轮循环中 iter = child_exprs.begin()
 *          不是这里的行为——实际上代码中有一个潜在 bug，
 *          因为 erase 后没有更新 iter 就结束了当前迭代，
 *          但函数立即 return 了，所以实际上没有继续循环）
 * ==========================================================================
 */

#include "sql/optimizer/conjunction_simplification_rule.h"
#include "common/log/log.h"
#include "sql/expr/expression.h"

/**
 * ★ try_to_get_bool_constant — 尝试从表达式中提取布尔常量
 *
 * 只有 VALUE 类型且值是 BOOLEANS 的表达式才能提取。
 * 如果是列引用或其他类型，返回 RC::INTERNAL 表示不是常量。
 */
RC try_to_get_bool_constant(unique_ptr<Expression> &expr, bool &constant_value)
{
  if (expr->type() == ExprType::VALUE && expr->value_type() == AttrType::BOOLEANS) {
    auto value_expr = static_cast<ValueExpr *>(expr.get());
    constant_value  = value_expr->get_value().get_boolean();
    return RC::SUCCESS;
  }
  return RC::INTERNAL;
}

RC ConjunctionSimplificationRule::rewrite(unique_ptr<Expression> &expr, bool &change_made)
{
  RC rc = RC::SUCCESS;
  if (expr->type() != ExprType::CONJUNCTION) {
    return rc;
  }

  change_made = false;
  auto                            conjunction_expr = static_cast<ConjunctionExpr *>(expr.get());
  vector<unique_ptr<Expression>> &child_exprs      = conjunction_expr->children();

  // ★ 遍历所有子表达式，检查常量 TRUE/FALSE
  for (auto iter = child_exprs.begin(); iter != child_exprs.end();) {
    bool constant_value = false;
    rc = try_to_get_bool_constant(*iter, constant_value);
    if (rc != RC::SUCCESS) {
      rc = RC::SUCCESS;
      ++iter;  // ★ 不是常量，跳过
      continue;
    }

    // ★ 处理 AND 连接
    if (conjunction_expr->conjunction_type() == ConjunctionExpr::Type::AND) {
      if (constant_value == true) {
        // ★ AND TRUE：消除恒真条件（如 a>3 AND TRUE → a>3）
        child_exprs.erase(iter);
      } else {
        // ★ AND FALSE：整个 AND 恒为 FALSE
        unique_ptr<Expression> child_expr = std::move(child_exprs.front());
        child_exprs.clear();
        expr = std::move(child_expr);
        return rc;
      }
    } else {
      // ★ 处理 OR 连接
      if (constant_value == true) {
        // ★ OR TRUE：整个 OR 恒为 TRUE
        unique_ptr<Expression> child_expr = std::move(child_exprs.front());
        child_exprs.clear();
        expr = std::move(child_expr);
        return rc;
      } else {
        // ★ OR FALSE：消除恒假条件
        child_exprs.erase(iter);
      }
    }
  }

  // ★ 折叠：只剩一个子表达式时，替换为子表达式本身（不需要 AND/OR 包装）
  if (child_exprs.size() == 1) {
    LOG_TRACE("conjunction expression has only 1 child");
    unique_ptr<Expression> child_expr = std::move(child_exprs.front());
    child_exprs.clear();
    expr = std::move(child_expr);
    change_made = true;
  }

  return rc;
}
