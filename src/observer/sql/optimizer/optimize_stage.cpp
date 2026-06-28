//
// Created by Longda on 2021/4/13.
//

/**
 * ==========================================================================
 * 【架构概览】OptimizeStage — 查询优化阶段
 * ==========================================================================
 *
 * ★ 定位：把"语义正确的语句"转换成"高效的执行计划"
 *
 * 这是一个三阶段流水线：
 *
 *   Stmt → create_logical_plan() → LogicalOperator（逻辑计划：做什么）
 *         → rewrite()               → LogicalOperator（规则重写：做更好）
 *         → generate_physical_plan() → PhysicalOperator（物理计划：怎么做）
 *
 * ★ 核心概念：逻辑算子 vs 物理算子
 *
 *   逻辑算子（LogicalOperator）：描述"要做什么"
 *     例：Project(c1,c2) → Predicate(id>10) → TableScan(t1)
 *     只关心数据的逻辑变换，不关心具体实现
 *
 *   物理算子（PhysicalOperator）：描述"怎么做"
 *     例：TableScan 使用全表扫描还是索引扫描？
 *         Join 使用 Hash Join 还是 Nested Loop Join？
 *     涉及具体算法选择、资源分配
 *
 * ★ 两套优化器：
 *   1. 规则优化器（RBO: Rule-Based Optimizer）
 *      - logical_plan_generator + rewriter + physical_plan_generator
 *      - 默认使用，速度快，确定性高
 *   2. Cascade 优化器（CBO: Cost-Based Optimizer）
 *      - 通过 use_cascade 开关控制
 *      - 基于代价估算选择最优计划
 *
 * ★ 向量化执行 vs 逐行执行
 *   在 generate_physical_plan 中根据 ExecutionMode 选择：
 *   - CHUNK_ITERATOR → 生成向量化算子（TABLE_SCAN_VEC, PROJECT_VEC 等）
 *   - 默认 → 生成火山模型算子（逐行迭代）
 *
 * 💡 提问：为什么 RBO 和 CBO 同时存在？直接全部用 CBO 不好吗？
 *   （提示：CBO 需要统计信息，对于小表/临时表收集统计信息值得吗？）
 *
 * 💡 提问：rewrite() 可能执行多次（do-while change_made），
 *   什么情况下一次 rewrite 不够？
 *   （提示：想想一条规则的处理结果可能是另一条规则的输入）
 * ==========================================================================
 */

#include <string.h>

#include "optimize_stage.h"

#include "common/conf/ini.h"
#include "common/io/io.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "event/session_event.h"
#include "event/sql_event.h"
#include "sql/operator/logical_operator.h"
#include "sql/stmt/stmt.h"
#include "sql/optimizer/cascade/optimizer.h"
#include "sql/optimizer/optimizer_utils.h"

using namespace std;
using namespace common;

/**
 * ★ 优化阶段入口
 *
 * 完整流程：
 *   1. 逻辑计划生成 — Stmt → LogicalOperator 树
 *   2. 规则重写     — 应用优化规则（谓词下推、列裁剪等）
 *   3. 物理计划生成 — LogicalOperator → PhysicalOperator 树
 *
 * ★ 两个关键的设计决策：
 *
 *   a) UNIMPLEMENTED 可跳过：
 *      DDL 语句（如 CREATE TABLE）在 create_logical_plan 中返回 UNIMPLEMENTED，
 *      因为没有对应的逻辑计划。优化器跳过这些语句，让 ExecuteStage 走 CommandExecutor。
 *
 *   b) 常量化子节点：
 *      generate_general_child() 尝试把能常量折叠的子节点直接计算出来。
 *      例如 "SELECT 1+2 FROM t1" 中，1+2 在优化阶段就算出结果 3，不需要每行都算。
 *
 * 💡 提问：如果逻辑计划生成成功但物理计划生成失败，怎么处理？
 *   有没有回退机制？比如 CBO 不行能不能退回到 RBO？
 */
RC OptimizeStage::handle_request(SQLStageEvent *sql_event)
{
  unique_ptr<LogicalOperator> logical_operator;

  // 步骤1: 生成逻辑计划
  RC rc = create_logical_plan(sql_event, logical_operator);
  if (rc != RC::SUCCESS) {
    if (rc != RC::UNIMPLEMENTED) {
      LOG_WARN("failed to create logical plan. rc=%s", strrc(rc));
    }
    return rc;  // UNIMPLEMENTED 返回给上层跳过优化（DDL 走 CommandExecutor）
  }

  ASSERT(logical_operator, "logical operator is null");

  // 步骤2: 规则重写（谓词下推、常量折叠等）
  rc = rewrite(logical_operator);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to rewrite plan. rc=%s", strrc(rc));
    return rc;
  }

  // ★ 常量折叠：把能预先计算的表达式直接算出来
  logical_operator->generate_general_child();

  // 步骤3: 生成物理计划
  unique_ptr<PhysicalOperator> physical_operator;

  // ★ 根据会话配置选择优化器类型
  if (sql_event->session_event()->session()->use_cascade()) {
    // CBO: 基于代价的 Cascade 优化器
    Optimizer optimizer;
    physical_operator = optimizer.optimize(logical_operator.get());
    if (!physical_operator) {
      rc = RC::INTERNAL;
      LOG_WARN("failed to optimize logical plan. rc=%s", strrc(rc));
      return rc;
    }

    string phys_plan_str = OptimizerUtils::dump_physical_plan(physical_operator);
    LOG_INFO("cascade physical plan:\n%s", phys_plan_str.c_str());
  } else {
    // RBO: 规则优化器（默认路径）
    rc = generate_physical_plan(logical_operator, physical_operator,
                                sql_event->session_event()->session());
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to generate physical plan. rc=%s", strrc(rc));
      return rc;
    }
  }

  // ★ 将物理执行计划存入事件，供 ExecuteStage 使用
  sql_event->set_operator(std::move(physical_operator));

  return rc;
}

RC OptimizeStage::optimize(unique_ptr<LogicalOperator> &oper)
{
  // do nothing — 可能留给子类扩展
  return RC::SUCCESS;
}

/**
 * ★ 生成物理计划
 *
 * 根据会话的执行模式选择：
 *   - CHUNK_ITERATOR → 向量化执行（批量处理，适合 OLAP）
 *   - 默认 → 火山模型（逐行处理，适合 OLTP）
 *
 * ★ 设计亮点：通过 Session 级别的开关切换执行模式，
 *    不需要改 SQL，不需要重新编译。
 *
 * 💡 提问：向量化执行一定比火山模型快吗？
 *   在什么场景下火山模型反而更好？
 *   （提示：SELECT * FROM t1 WHERE id = 1 LIMIT 1）
 */
RC OptimizeStage::generate_physical_plan(
    unique_ptr<LogicalOperator> &logical_operator,
    unique_ptr<PhysicalOperator> &physical_operator,
    Session *session)
{
  RC rc = RC::SUCCESS;
  if (session->get_execution_mode() == ExecutionMode::CHUNK_ITERATOR
      && LogicalOperator::can_generate_vectorized_operator(logical_operator->type())) {
    LOG_TRACE("use chunk iterator");
    session->set_used_chunk_mode(true);
    rc = physical_plan_generator_.create_vec(*logical_operator, physical_operator, session);
  } else {
    LOG_TRACE("use tuple iterator");
    session->set_used_chunk_mode(false);
    rc = physical_plan_generator_.create(*logical_operator, physical_operator, session);
  }
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to create physical operator. rc=%s", strrc(rc));
  }
  return rc;
}

/**
 * ★ 规则重写
 *
 * 使用 do-while 循环反复应用重写规则，直到没有变化为止。
 * 这称为"不动点迭代"（Fixed-Point Iteration），因为：
 *   规则 A 的结果可能是规则 B 的输入，
 *   规则 B 的结果又可能是规则 A 的新输入。
 * 每一轮都可能有新的优化机会。
 *
 * 💡 提问：如果两条规则互相触发（A→B→A→B→...），会不会死循环？
 *   change_made 是怎么保证终止的？
 *   （提示：每次重写都应该让计划"更优"，什么度量能保证不会无限循环？）
 */
RC OptimizeStage::rewrite(unique_ptr<LogicalOperator> &logical_operator)
{
  RC rc = RC::SUCCESS;

  bool change_made = false;
  do {
    change_made = false;
    rc          = rewriter_.rewrite(logical_operator, change_made);
    if (rc != RC::SUCCESS) {
      LOG_WARN("failed to do expression rewrite on logical plan. rc=%s", strrc(rc));
      return rc;
    }
  } while (change_made);  // ★ 只要还有变化就继续优化

  return rc;
}

/**
 * ★ 生成逻辑计划
 *
 * 根据 Stmt 类型（SELECT/INSERT/DELETE等）生成对应的逻辑算子。
 * 由 LogicalPlanGenerator 完成具体转换。
 */
RC OptimizeStage::create_logical_plan(SQLStageEvent *sql_event,
    unique_ptr<LogicalOperator> &logical_operator)
{
  Stmt *stmt = sql_event->stmt();
  if (nullptr == stmt) {
    return RC::UNIMPLEMENTED;
  }

  return logical_plan_generator_.create(stmt, logical_operator);
}
