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
 * ★★★ ComparisonSimplificationRule — 比较表达式简化 ★★★
 * ==========================================================================
 *
 * ★ 作用：常量比较折叠（Constant Folding 的一种）
 *
 * 如果一个 COMPARISON 表达式两边的值都是编译期已知的常量，
 * 就尝试把它计算出结果，替换为一个常量值。
 *
 * ★ 示例：
 *   1 > 0     →  TRUE
 *   3 = 5     →  FALSE
 *   'abc' < 'a' → FALSE（如果字符串比较支持的话）
 *
 * ★ 为什么在表达式重写阶段做？
 *   Filter(c > 10) 中的 "c > 10" 因为 c 是变量所以不能折叠，
 *   但 Filter(1 > 0) 中的 "1 > 0" 两边都是常量——可以直接算出 TRUE，
 *   然后整个 Filter 节点变为恒真，后续 PredicateRewrite 会消除它。
 *
 * ★ try_get_value 的工作方式：
 *   ComparisonExpr::try_get_value(Value &v) 检查左右子表达式是否都是常量值，
 *   如果是，执行比较并把结果写入 v。
 *   如果任何一边不是常量（如列引用），返回非 SUCCESS。
 *
 * 💡 提问：为什么只处理 CmpExpr::try_get_value 能成功的情况？
 *   （提示：try_get_value 内部检查了左右是否都是 VALUE 类型。
 *          如果是 列引用（FIELD），try_get_value 返回失败，
 *          规则不做任何修改——因为列的值在计划时未知，必须在运行时比较）
 * ==========================================================================
 */

#include "sql/optimizer/comparison_simplification_rule.h"
#include "common/log/log.h"
#include "sql/expr/expression.h"

RC ComparisonSimplificationRule::rewrite(unique_ptr<Expression> &expr, bool &change_made)
{
  RC rc = RC::SUCCESS;

  change_made = false;
  if (expr->type() == ExprType::COMPARISON) {
    Value value;

    ComparisonExpr *cmp_expr = static_cast<ComparisonExpr *>(expr.get());

    // ★ try_get_value：如果两边都是常量值，执行比较并返回结果
    RC sub_rc = cmp_expr->try_get_value(value);
    if (sub_rc == RC::SUCCESS) {
      // ★ 比较结果是常量，替换为 ValueExpr(TRUE/FALSE)
      unique_ptr<Expression> new_expr(new ValueExpr(value));
      expr.swap(new_expr);  // ★ 用常量替换整个比较表达式
      change_made = true;
      LOG_TRACE("comparison expression is simplified");
    }
  }
  return rc;
}
