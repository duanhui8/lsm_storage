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
 * ★★★ Rewriter — 逻辑计划重写器（规则引擎）★★★
 * ==========================================================================
 *
 * ★ 文件定位：
 *   在逻辑计划生成后、物理计划生成前，对逻辑计划树做"形状优化"。
 *   这是 OptimizeStage 中 `rewrite(logical_operator)` 调用的入口。
 *
 * ★ 与 OptimizeStage 的关系：
 *   handle_request() 中调用顺序：
 *     create_logical_plan()  →  logical_operator
 *     rewrite()              →  优化后的 logical_operator
 *     generate_physical_plan() →  physical_operator
 *
 * ★ 三个重写规则（按注册顺序）：
 *   1. ExpressionRewriter     — 表达式级简化（常量折叠、合取简化）
 *   2. PredicateRewriteRule   — 谓词等价变换（a>3 → 3<a 等规范化）
 *   3. PredicatePushdownRewriter — 谓词下推（把 Filter 推到 Scan 层）
 *
 * ★ 递归策略：
 *   对每个 LogicalOperator 节点：
 *     1. 用所有规则重写当前节点
 *     2. 递归重写所有子节点
 *   如果任何规则产生了变化（change_made=true），标记传播到上层。
 *
 * ★ change_made 的作用：
 *   有些优化场景需要"迭代优化"——因为一次重写可能产生新的优化机会。
 *   外层调用者可以用 change_made 决定是否要再跑一轮 rewrite。
 *
 * 💡 提问：为什么重写顺序是 Expression → Predicate → Pushdown？
 *   （提示：先简化表达式，后下推谓词。如果先下推后简化，
 *          可能错失一些简化机会，因为下推后的谓词分布不同了）
 * ==========================================================================
 */

#include "sql/optimizer/rewriter.h"
#include "common/log/log.h"
#include "sql/operator/logical_operator.h"
#include "sql/optimizer/expression_rewriter.h"
#include "sql/optimizer/predicate_pushdown_rewriter.h"
#include "sql/optimizer/predicate_rewrite.h"

/**
 * ★ Rewriter 构造函数 — 注册所有规则
 *
 * 规则按注册顺序执行。
 * 如果需要新增优化规则，在这里添加即可。
 */
Rewriter::Rewriter()
{
  rewrite_rules_.emplace_back(new ExpressionRewriter);         // ★ 表达式简化（常量折叠、AND/OR 简化）
  rewrite_rules_.emplace_back(new PredicateRewriteRule);       // ★ 谓词规范化
  rewrite_rules_.emplace_back(new PredicatePushdownRewriter);  // ★ 谓词下推到 Scan
}

/**
 * ★★★ rewrite — 递归重写逻辑计划树 ★★★
 *
 * 遍历顺序：先处理当前节点（pre-order），再递归子节点。
 * 每个规则都可以修改 oper 本身（如把 Filter 拆开、下推表达式到子节点）。
 *
 * @param oper        当前要重写的逻辑算子（可能被规则修改）
 * @param change_made [out] 是否产生了任何修改
 */
RC Rewriter::rewrite(unique_ptr<LogicalOperator> &oper, bool &change_made)
{
  RC rc = RC::SUCCESS;

  change_made = false;
  // ★ 第一步：对当前节点应用所有规则
  for (unique_ptr<RewriteRule> &rule : rewrite_rules_) {
    bool sub_change_made = false;

    rc = rule->rewrite(oper, sub_change_made);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to rewrite logical operator. rc=%s", strrc(rc));
      return rc;
    }

    if (sub_change_made && !change_made) {
      change_made = true;
    }
  }

  if (rc != RC::SUCCESS) {
    return rc;
  }

  // ★ 第二步：递归重写所有子节点
  vector<unique_ptr<LogicalOperator>> &child_opers = oper->children();
  for (auto &child_oper : child_opers) {
    bool sub_change_made = false;
    rc                   = this->rewrite(child_oper, sub_change_made);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to rewrite child oper. rc=%s", strrc(rc));
      return rc;
    }

    if (sub_change_made && !change_made) {
      change_made = true;
    }
  }
  return rc;
}
