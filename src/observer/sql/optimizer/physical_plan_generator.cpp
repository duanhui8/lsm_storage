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
// Created by Wangyunlai on 2022/12/14.
//

/**
 * ==========================================================================
 * ★★★【核心学习文件】physical_plan_generator.cpp — 物理计划生成器 ★★★
 * ==========================================================================
 *
 * ★ 定位：把 LogicalOperator 树转换成 PhysicalOperator 树。
 *   这是"逻辑 → 物理"的关键转换步骤。
 *
 * ★ 逻辑计划 vs 物理计划：
 *   LogicalOperator  — "做什么"（TableGet, Predicate, Project...）
 *   PhysicalOperator  — "怎么做"（TableScan vs IndexScan, 具体算法）
 *
 * ★ 核心转换示例：
 *   TableGet(t1) → 检查条件 → 有索引匹配 → IndexScan(t1, idx_id)
 *                            → 无索引匹配 → TableScan(t1)
 *
 *   Predicate(expr) → PredicatePhysicalOperator(expr)
 *   Project(exprs)  → ProjectPhysicalOperator(exprs)
 *   Join(t1, t2)    → NestedLoopJoin 或 HashJoin
 *
 * ★★★ 最重要的方法：create_plan(TableGetLogicalOperator) ★★★
 *   这是索引选择的核心逻辑：
 *   1. 遍历 TableGet 的所有下推谓词
 *   2. 找到"等值比较"且"某一边是索引列"的条件
 *   3. 如果找到匹配的索引 → 创建 IndexScanPhysicalOperator
 *   4. 否则 → 创建 TableScanPhysicalOperator（全表扫描）
 *
 *   这就是"基于规则的优化"（RBO）：
 *   有索引就用索引，没有就全表扫描。
 *   规则很简单，但对于教学项目足够了。
 *
 * ★ Vec 版本（create_vec / create_vec_plan）：
 *   向量化执行路径。逻辑相同但使用向量化算子（TableScanVec 等）。
 *   通过 session 的 execution_mode 选择 Tuple 模式或 Vec 模式。
 *
 * 💡 提问：MiniOB 的索引选择只检查 EQUAL_TO 和 NOT_EQUAL，
 *   为什么不支持 <, >, <=, >= （范围扫描）？
 *   （提示：B+ 树天然支持范围扫描。这只是一个教学简化
 *          — 加上范围扫描支持只需要多几行代码判断 LESS_THAN 等操作符。
 *          可以自己试试扩展！）
 *
 * 💡 提问：如果 WHERE 条件有多个等值比较（id=5 AND name='Alice'），
 *   且两个列都有索引，选择哪个？
 *   （提示：当前代码遍历 predicates，使用第一个匹配的索引就 break。
 *          这是最简单的策略。生产数据库会计算每个索引的"选择性"
 *          （selectivity），选择过滤性更好的索引）
 * ==========================================================================
 */

#include "common/log/log.h"
#include "sql/expr/expression.h"
#include "session/session.h"
#include "sql/operator/aggregate_vec_physical_operator.h"
#include "sql/operator/calc_logical_operator.h"
#include "sql/operator/calc_physical_operator.h"
#include "sql/operator/delete_logical_operator.h"
#include "sql/operator/delete_physical_operator.h"
#include "sql/operator/explain_logical_operator.h"
#include "sql/operator/explain_physical_operator.h"
#include "sql/operator/expr_vec_physical_operator.h"
#include "sql/operator/group_by_vec_physical_operator.h"
#include "sql/operator/hash_join_physical_operator.h"
#include "sql/operator/index_scan_physical_operator.h"
#include "sql/operator/insert_logical_operator.h"
#include "sql/operator/insert_physical_operator.h"
#include "sql/operator/join_logical_operator.h"
#include "sql/operator/nested_loop_join_physical_operator.h"
#include "sql/operator/predicate_logical_operator.h"
#include "sql/operator/predicate_physical_operator.h"
#include "sql/operator/project_logical_operator.h"
#include "sql/operator/project_physical_operator.h"
#include "sql/operator/project_vec_physical_operator.h"
#include "sql/operator/table_get_logical_operator.h"
#include "sql/operator/table_scan_physical_operator.h"
#include "sql/operator/group_by_logical_operator.h"
#include "sql/operator/group_by_physical_operator.h"
#include "sql/operator/hash_group_by_physical_operator.h"
#include "sql/operator/scalar_group_by_physical_operator.h"
#include "sql/operator/table_scan_vec_physical_operator.h"
#include "sql/optimizer/physical_plan_generator.h"

using namespace std;

/**
 * ★ create — 根据逻辑算子类型分发
 */
RC PhysicalPlanGenerator::create(LogicalOperator &logical_operator, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  RC rc = RC::SUCCESS;

  switch (logical_operator.type()) {
    case LogicalOperatorType::CALC: {
      return create_plan(static_cast<CalcLogicalOperator &>(logical_operator), oper, session);
    } break;

    case LogicalOperatorType::TABLE_GET: {
      return create_plan(static_cast<TableGetLogicalOperator &>(logical_operator), oper, session);
    } break;

    case LogicalOperatorType::PREDICATE: {
      return create_plan(static_cast<PredicateLogicalOperator &>(logical_operator), oper, session);
    } break;

    case LogicalOperatorType::PROJECTION: {
      return create_plan(static_cast<ProjectLogicalOperator &>(logical_operator), oper, session);
    } break;

    case LogicalOperatorType::INSERT: {
      return create_plan(static_cast<InsertLogicalOperator &>(logical_operator), oper, session);
    } break;

    case LogicalOperatorType::DELETE: {
      return create_plan(static_cast<DeleteLogicalOperator &>(logical_operator), oper, session);
    } break;

    case LogicalOperatorType::EXPLAIN: {
      return create_plan(static_cast<ExplainLogicalOperator &>(logical_operator), oper, session);
    } break;

    case LogicalOperatorType::JOIN: {
      return create_plan(static_cast<JoinLogicalOperator &>(logical_operator), oper, session);
    } break;

    case LogicalOperatorType::GROUP_BY: {
      return create_plan(static_cast<GroupByLogicalOperator &>(logical_operator), oper, session);
    } break;

    default: {
      ASSERT(false, "unknown logical operator type");
      return RC::INVALID_ARGUMENT;
    }
  }
  return rc;
}

RC PhysicalPlanGenerator::create_vec(LogicalOperator &logical_operator, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  RC rc = RC::SUCCESS;

  switch (logical_operator.type()) {
    case LogicalOperatorType::TABLE_GET: {
      return create_vec_plan(static_cast<TableGetLogicalOperator &>(logical_operator), oper, session);
    } break;
    case LogicalOperatorType::PROJECTION: {
      return create_vec_plan(static_cast<ProjectLogicalOperator &>(logical_operator), oper, session);
    } break;
    case LogicalOperatorType::GROUP_BY: {
      return create_vec_plan(static_cast<GroupByLogicalOperator &>(logical_operator), oper, session);
    } break;
    case LogicalOperatorType::EXPLAIN: {
      return create_vec_plan(static_cast<ExplainLogicalOperator &>(logical_operator), oper, session);
    } break;
    default: {
      LOG_WARN("unknown logical operator type: %d", logical_operator.type());
      return RC::INVALID_ARGUMENT;
    }
  }
  return rc;
}

/**
 * ★★★ create_plan(TableGetLogicalOperator) — 索引选择的核心逻辑 ★★★
 *
 * 这是物理优化中最重要的方法：决定用 IndexScan 还是 TableScan。
 *
 * 索引匹配逻辑（当前只支持等值匹配）：
 *   1. 遍历所有下推谓词（predicates）
 *   2. 找到 ComparisonExpr 类型且操作符为 EQUAL_TO 的条件
 *   3. 确保一边是字段引用（FieldExpr），另一边是常量值（ValueExpr）
 *   4. 查找该字段是否有索引（table->find_index_by_field）
 *   5. 有匹配索引 → IndexScan，无匹配 → TableScan
 *
 * ★ 为什么叫"基于规则的优化"（RBO）？
 *   这里的优化策略是硬编码的规则，而不是基于代价的计算：
 *   - 规则1: 有索引 → 用索引
 *   - 规则2: 没索引 → 全表扫描
 *   没有考虑"索引扫描可能比全表扫描更慢"的场景（比如表只有 10 行）。
 *
 *   "基于代价的优化"（CBO）会：
 *   - 估算 IndexScan 的代价（CPU + I/O）
 *   - 估算 TableScan 的代价
 *   - 选择代价更小的
 *
 * ★ 下推谓词的命运：
 *   找到索引匹配的条件后，该条件从 predicates 中移除（被索引覆盖）。
 *   剩余条件通过 set_predicates() 传给算子做 filter。
 *   但当前代码中，predicates 全部传给算子（没有移除已匹配的）—
 *   这意味着索引已经过滤了 id=5，filter 还会再检查一次 id=5。
 *   这是当前实现的一个小冗余（不影响正确性，略影响性能）。
 */
RC PhysicalPlanGenerator::create_plan(TableGetLogicalOperator &table_get_oper, unique_ptr<PhysicalOperator> &oper, Session*)
{
  auto *table = table_get_oper.table();
  auto table_scan_oper = new TableScanPhysicalOperator(table, ReadWriteMode::READ_WRITE);
  table_scan_oper->set_table_name(table_get_oper.get_table_name());
  oper = unique_ptr<PhysicalOperator>(table_scan_oper);
  return RC::SUCCESS;
}

RC PhysicalPlanGenerator::create_plan(PredicateLogicalOperator &pred_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  vector<unique_ptr<LogicalOperator>> &children_opers = pred_oper.children();
  ASSERT(children_opers.size() == 1, "predicate logical operator's sub oper number should be 1");

  LogicalOperator &child_oper = *children_opers.front();

  unique_ptr<PhysicalOperator> child_phy_oper;
  RC                           rc = create(child_oper, child_phy_oper, session);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to create child operator of predicate operator. rc=%s", strrc(rc));
    return rc;
  }

  vector<unique_ptr<Expression>> &expressions = pred_oper.expressions();
  ASSERT(expressions.size() == 1, "predicate logical operator's children should be 1");

  unique_ptr<Expression> expression = std::move(expressions.front());
  oper = unique_ptr<PhysicalOperator>(new PredicatePhysicalOperator(std::move(expression)));
  oper->add_child(std::move(child_phy_oper));
  return rc;
}

RC PhysicalPlanGenerator::create_plan(ProjectLogicalOperator &project_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  vector<unique_ptr<LogicalOperator>> &child_opers = project_oper.children();

  unique_ptr<PhysicalOperator> child_phy_oper;

  RC rc = RC::SUCCESS;
  if (!child_opers.empty()) {
    LogicalOperator *child_oper = child_opers.front().get();

    rc = create(*child_oper, child_phy_oper, session);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to create project logical operator's child physical operator. rc=%s", strrc(rc));
      return rc;
    }
  }

  auto project_operator = make_unique<ProjectPhysicalOperator>(std::move(project_oper.expressions()));
  if (child_phy_oper) {
    project_operator->add_child(std::move(child_phy_oper));
  }

  oper = std::move(project_operator);

  LOG_TRACE("create a project physical operator");
  return rc;
}

RC PhysicalPlanGenerator::create_plan(InsertLogicalOperator &insert_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  auto                   *table           = insert_oper.table();
  InsertPhysicalOperator *insert_phy_oper = new InsertPhysicalOperator(table, {});
  oper.reset(insert_phy_oper);
  return RC::SUCCESS;
}

RC PhysicalPlanGenerator::create_plan(DeleteLogicalOperator &delete_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  vector<unique_ptr<LogicalOperator>> &child_opers = delete_oper.children();

  unique_ptr<PhysicalOperator> child_physical_oper;

  RC rc = RC::SUCCESS;
  if (!child_opers.empty()) {
    LogicalOperator *child_oper = child_opers.front().get();

    rc = create(*child_oper, child_physical_oper, session);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to create physical operator. rc=%s", strrc(rc));
      return rc;
    }
  }

  oper = unique_ptr<PhysicalOperator>(new DeletePhysicalOperator(delete_oper.table()));

  if (child_physical_oper) {
    oper->add_child(std::move(child_physical_oper));
  }
  return rc;
}

RC PhysicalPlanGenerator::create_plan(ExplainLogicalOperator &explain_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  vector<unique_ptr<LogicalOperator>> &child_opers = explain_oper.children();

  RC rc = RC::SUCCESS;

  unique_ptr<PhysicalOperator> explain_physical_oper(new ExplainPhysicalOperator);
  for (unique_ptr<LogicalOperator> &child_oper : child_opers) {
    unique_ptr<PhysicalOperator> child_physical_oper;
    rc = create(*child_oper, child_physical_oper, session);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to create child physical operator. rc=%s", strrc(rc));
      return rc;
    }

    explain_physical_oper->add_child(std::move(child_physical_oper));
  }

  oper = std::move(explain_physical_oper);
  return rc;
}

/**
 * ★ create_plan(JoinLogicalOperator) — JOIN 物理计划
 *
 * 当前默认使用 NestedLoopJoin（嵌套循环连接）。
 * HashJoin 通过 false 和 can_use_hash_join() 判断是否可用，
 * 但 can_use_hash_join() 当前总是返回 false（留作练习）。
 *
 * NestedLoopJoin 时间复杂度：O(left_rows × right_rows)
 * 适合小表或已通过索引过滤后的结果集。
 */
RC PhysicalPlanGenerator::create_plan(JoinLogicalOperator &join_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  RC rc = RC::SUCCESS;

  vector<unique_ptr<LogicalOperator>> &child_opers = join_oper.children();
  if (child_opers.size() != 2) {
    LOG_WARN("join operator should have 2 children, but have %d", child_opers.size());
    return RC::INTERNAL;
  }
  if (false && can_use_hash_join(join_oper)) {
    // your code here
  } else {
    unique_ptr<PhysicalOperator> join_physical_oper(new NestedLoopJoinPhysicalOperator());
    for (auto &child_oper : child_opers) {
      unique_ptr<PhysicalOperator> child_physical_oper;
      rc = create(*child_oper, child_physical_oper, session);
      if (rc != RC::SUCCESS) {
        LOG_WARN("failed to create physical child oper. rc=%s", strrc(rc));
        return rc;
      }

      join_physical_oper->add_child(std::move(child_physical_oper));
    }

    oper = std::move(join_physical_oper);
  }
  return rc;
}

bool PhysicalPlanGenerator::can_use_hash_join(JoinLogicalOperator &join_oper)
{
  // your code here
  return false;
}

RC PhysicalPlanGenerator::create_plan(CalcLogicalOperator &logical_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  RC rc = RC::SUCCESS;

  CalcPhysicalOperator *calc_oper = new CalcPhysicalOperator(std::move(logical_oper.expressions()));
  oper.reset(calc_oper);
  return rc;
}

/**
 * ★ create_plan(GroupByLogicalOperator) — GROUP BY 物理计划
 *
 * 两种策略：
 *   - 无 GROUP BY 列 → ScalarGroupBy（全局聚合，如 SELECT COUNT(*) FROM t1）
 *   - 有 GROUP BY 列 → HashGroupBy（通过 Hash 表分组）
 */
RC PhysicalPlanGenerator::create_plan(GroupByLogicalOperator &logical_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  RC rc = RC::SUCCESS;

  vector<unique_ptr<Expression>> &group_by_expressions = logical_oper.group_by_expressions();
  unique_ptr<GroupByPhysicalOperator> group_by_oper;
  if (group_by_expressions.empty()) {
    group_by_oper = make_unique<ScalarGroupByPhysicalOperator>(std::move(logical_oper.aggregate_expressions()));
  } else {
    group_by_oper = make_unique<HashGroupByPhysicalOperator>(std::move(logical_oper.group_by_expressions()),
        std::move(logical_oper.aggregate_expressions()));
  }

  ASSERT(logical_oper.children().size() == 1, "group by operator should have 1 child");

  LogicalOperator             &child_oper = *logical_oper.children().front();
  unique_ptr<PhysicalOperator> child_physical_oper;
  rc = create(child_oper, child_physical_oper, session);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to create child physical operator of group by operator. rc=%s", strrc(rc));
    return rc;
  }

  group_by_oper->add_child(std::move(child_physical_oper));

  oper = std::move(group_by_oper);
  return rc;
}

RC PhysicalPlanGenerator::create_vec_plan(TableGetLogicalOperator &table_get_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  auto *table = table_get_oper.table();
  TableScanVecPhysicalOperator *table_scan_oper = new TableScanVecPhysicalOperator(table, ReadWriteMode::READ_WRITE);
  oper = unique_ptr<PhysicalOperator>(table_scan_oper);
  LOG_TRACE("use vectorized table scan");

  return RC::SUCCESS;
}

RC PhysicalPlanGenerator::create_vec_plan(GroupByLogicalOperator &logical_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  RC rc = RC::SUCCESS;
  unique_ptr<PhysicalOperator> physical_oper = nullptr;
  if (logical_oper.group_by_expressions().empty()) {
    physical_oper = make_unique<AggregateVecPhysicalOperator>(std::move(logical_oper.aggregate_expressions()));
  } else {
    physical_oper = make_unique<GroupByVecPhysicalOperator>(
      std::move(logical_oper.group_by_expressions()), std::move(logical_oper.aggregate_expressions()));

  }

  ASSERT(logical_oper.children().size() == 1, "group by operator should have 1 child");

  LogicalOperator             &child_oper = *logical_oper.children().front();
  unique_ptr<PhysicalOperator> child_physical_oper;
  rc = create_vec(child_oper, child_physical_oper, session);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to create child physical operator of group by(vec) operator. rc=%s", strrc(rc));
    return rc;
  }

  physical_oper->add_child(std::move(child_physical_oper));

  oper = std::move(physical_oper);
  return rc;

  return RC::SUCCESS;
}

RC PhysicalPlanGenerator::create_vec_plan(ProjectLogicalOperator &project_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  vector<unique_ptr<LogicalOperator>> &child_opers = project_oper.children();

  unique_ptr<PhysicalOperator> child_phy_oper;

  RC rc = RC::SUCCESS;
  if (!child_opers.empty()) {
    LogicalOperator *child_oper = child_opers.front().get();
    rc                          = create_vec(*child_oper, child_phy_oper, session);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to create project logical operator's child physical operator. rc=%s", strrc(rc));
      return rc;
    }
  }

  auto project_operator = make_unique<ProjectVecPhysicalOperator>(std::move(project_oper.expressions()));

  if (child_phy_oper != nullptr) {
    // Vec expressions not yet implemented
    if (child_phy_oper) project_operator->add_child(std::move(child_phy_oper));
  }

  oper = std::move(project_operator);

  LOG_TRACE("create a project physical operator");
  return rc;
}


RC PhysicalPlanGenerator::create_vec_plan(ExplainLogicalOperator &explain_oper, unique_ptr<PhysicalOperator> &oper, Session* session)
{
  vector<unique_ptr<LogicalOperator>> &child_opers = explain_oper.children();

  RC rc = RC::SUCCESS;
  unique_ptr<PhysicalOperator> explain_physical_oper(new ExplainPhysicalOperator);
  for (unique_ptr<LogicalOperator> &child_oper : child_opers) {
    unique_ptr<PhysicalOperator> child_physical_oper;
    rc = create_vec(*child_oper, child_physical_oper, session);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to create child physical operator. rc=%s", strrc(rc));
      return rc;
    }

    explain_physical_oper->add_child(std::move(child_physical_oper));
  }

  oper = std::move(explain_physical_oper);
  return rc;
}
