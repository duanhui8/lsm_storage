//
// Created by Longda on 2021/4/13.
//

/**
 * ==========================================================================
 * 【架构概览】ExecuteStage — SQL 执行阶段
 * ==========================================================================
 *
 * 这是 SQL 流水线的最后一个阶段：把优化器生成的"执行计划"真正跑起来。
 *
 * ★ 核心概念：物理算子（PhysicalOperator）— 执行计划的具体执行单元
 *
 * 优化器生成了一个算子树，比如:
 *       Project (投影：只要 id, name 列)
 *         ↑
 *       Predicate (过滤：id > 10)
 *         ↑
 *       TableScan (扫描：逐行读取 t1 表)
 *
 * 这棵树的执行过程：
 *   Project.next() → Predicate.next() → TableScan.next()
 *                   拿到一行 → 检查 id>10? → 去掉不要的列 → 返回给用户
 *
 * ★ 关键区分：ExecuteStage 本身不"执行"查询，它只是把算子"转交"给 SqlResult。
 *   真正的执行（一行一行拉取数据）发生在 write_result 阶段，由通信层触发。
 *
 * ★ 两路分发：
 *   1. 有 physical_operator → handle_request_with_physical_operator （SELECT/DELETE/UPDATE）
 *   2. 无 physical_operator → CommandExecutor（DDL 如 CREATE TABLE，管理命令如 HELP）
 *
 * 💡 提问：为什么不直接在 ExecuteStage 里执行，而要"转交"给 SqlResult？
 *    （提示：想想数据的消费方是谁？如果数据在 ExecuteStage 全部算完存起来会有什么问题？）
 * ==========================================================================
 */

#include "sql/executor/execute_stage.h"

#include "common/log/log.h"
#include "event/session_event.h"
#include "event/sql_event.h"
#include "sql/executor/command_executor.h"
#include "sql/operator/calc_physical_operator.h"
#include "sql/stmt/select_stmt.h"
#include "sql/stmt/stmt.h"

using namespace common;

/**
 * ★ 执行阶段入口
 *
 * 这里有一个重要的分支判断：
 *   - 如果优化器生成了物理算子 → 走算子执行路径（DML：SELECT/INSERT/DELETE/UPDATE）
 *   - 如果没有物理算子       → 走命令执行路径（DDL：CREATE TABLE/DROP TABLE 等）
 *
 * 为什么有的语句有算子、有的没有？
 *   DML 语句需要"从存储引擎读数据"→"过滤"→"投影"，这些操作抽象为算子
 *   DDL 语句只是"创建一个表"或"删除一个索引"，不需要数据流，直接调存储引擎 API 即可
 */
RC ExecuteStage::handle_request(SQLStageEvent *sql_event)
{
  RC rc = RC::SUCCESS;

  const unique_ptr<PhysicalOperator> &physical_operator = sql_event->physical_operator();
  if (physical_operator != nullptr) {
    // ★ DML 路径：有执行计划
    return handle_request_with_physical_operator(sql_event);
  }

  SessionEvent *session_event = sql_event->session_event();

  Stmt *stmt = sql_event->stmt();
  if (stmt != nullptr) {
    // ★ DDL 路径：直接执行命令
    CommandExecutor command_executor;
    rc = command_executor.execute(sql_event);
    session_event->sql_result()->set_return_code(rc);
  } else {
    return RC::INTERNAL;
  }
  return rc;
}

/**
 * ★ 算子执行路径
 *
 * 这个函数非常短，只有一行关键操作 —— 把算子树"转交"给 SqlResult：
 *
 *   sql_result->set_operator(std::move(physical_operator));
 *
 * 这里只是"交接"，并不真正执行！真正的执行（open/next/close 循环）发生在后面，
 * 当通信层调用 SqlResult::open() 和 SqlResult::next_tuple() 时。
 *
 * ★ 为什么是"拉取模型"（Pull-Based Iterator / 火山模型）而不是"推送模型"？
 *
 *   火山模型（拉取）：
 *     上层每次调用 next()，算子计算一行返回
 *     优点：按需计算，可以直接中断（LIMIT 5 只需要算 5 次）
 *     缺点：虚函数调用开销（每行都要调多次 next）
 *
 *   推送模型（批量）：
 *     算子一次性算完所有行，推送给上层
 *     优点：可以批量优化（向量化），函数调用少
 *     缺点：无法提前终止，内存占用大
 *
 *   MiniOB 默认用火山模型，但也有向量化版本（TABLE_SCAN_VEC, PROJECT_VEC 等）。
 *
 * 💡 提问：如果需要查询前 5 行就停止，火山模型和推送模型各需要多少内存？
 *    （假设表有 100 万行数据）
 * ==========================================================================
 */
RC ExecuteStage::handle_request_with_physical_operator(SQLStageEvent *sql_event)
{
  RC rc = RC::SUCCESS;

  // 从事件中取出优化器生成的物理执行计划
  // ★ 注意这里是 unique_ptr，用 std::move 后，sql_event 里的算子就被清空了
  // 这保证了"同一份执行计划只能被执行一次"
  unique_ptr<PhysicalOperator> &physical_operator = sql_event->physical_operator();
  ASSERT(physical_operator != nullptr, "physical operator should not be null");

  // ★ 核心操作：把算子树交给 SqlResult
  // SqlResult 是"查询结果的容器 + 执行驱动器"
  // 调用方后续通过 sql_result->open() → next_tuple() → close() 来拉取数据
  SqlResult *sql_result = sql_event->session_event()->sql_result();
  sql_result->set_operator(std::move(physical_operator));
  return rc;
}
