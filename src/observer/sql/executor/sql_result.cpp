//
// Created by WangYunlai on 2022/11/18.
//

/**
 * ==========================================================================
 * 【架构概览】SqlResult — 查询结果容器 + 火山模型驱动器
 * ==========================================================================
 *
 * SqlResult 有两个角色：
 *
 * 角色一：结果容器
 *   存储查询的元数据（列名、列类型）和返回状态
 *
 * 角色二：火山模型驱动器 ★ 这是关键
 *   配合通信层完成"打开→逐行拉取→关闭"的迭代过程：
 *
 *   通信层调用                      SqlResult 转发
 *   ─────────────────────────────────────────────────
 *   sql_result->open()       →     operator_->open(trx)
 *   sql_result->next_tuple() →     operator_->next()
 *   sql_result->close()      →     operator_->close()
 *                                       ↓
 *                                  根据情况 commit 或 rollback 事务
 *
 * ★ 设计亮点：事务的自动管理
 *   在 close() 中，根据执行结果自动决定 commit 还是 rollback：
 *   - 执行成功 → commit（事务提交，修改生效）
 *   - 执行失败 → rollback（事务回滚，修改撤销）
 *   调用方不需要关心事务生命周期，减少了遗漏提交/回滚的 bug。
 *
 *   💡 提问：如果用户想在一个事务里执行多条 SQL（BEGIN → SQL1 → SQL2 → COMMIT），
 *      这个自动 commit/rollback 的逻辑还适用吗？
 *      （提示：看 is_trx_multi_operation_mode 这个判断条件）
 * ==========================================================================
 */

#include "sql/executor/sql_result.h"
#include "common/log/log.h"
#include "common/sys/rc.h"
#include "session/session.h"

SqlResult::SqlResult(Session *session) : session_(session) {}

void SqlResult::set_tuple_schema(const TupleSchema &schema) { tuple_schema_ = schema; }

/**
 * ★ 打开执行计划
 *
 * 这里做了两件事：
 * 1. 启动事务（如果还没启动）
 *    start_if_need() 是一个"懒启动"设计：事务不是收到 SQL 就创建，
 *    而是真正要读写数据时才创建。这样纯查询在没有写操作时开销更小。
 * 2. 调 operator_->open(trx)
 *    初始化算子内部状态。比如 TableScan::open() 会打开表、定位到第一行
 *
 * @return RC::INVALID_ARGUMENT 如果还没设置算子树
 */
RC SqlResult::open()
{
  if (nullptr == operator_) {
    return RC::INVALID_ARGUMENT;
  }

  // Set tuple schema from the operator
  RC rc = operator_->tuple_schema(tuple_schema_);
  LOG_INFO("SqlResult::open: operator_->tuple_schema returned %d, cell_num=%d", rc, tuple_schema_.cell_num());

  Trx *trx = session_->current_trx();
  trx->start_if_need();
  return operator_->open(trx);
}

/**
 * ★ 关闭执行计划 + 自动事务管理
 *
 * 执行流程：
 * 1. 调用 operator_->close()  — 释放算子资源（如关闭表扫描的游标）
 * 2. 销毁算子树（operator_.reset()）— 释放内存
 * 3. ★ 根据执行结果处理事务：
 *    - 多语句事务模式：不自动提交/回滚，等用户显式 COMMIT/ROLLBACK
 *    - 单语句模式：成功则 commit，失败则 rollback，然后销毁事务对象
 *
 * ★ 设计思想：RAII 风格的资源管理
 *    算子的 open() 在 SqlResult::open() 中调用，
 *    算子的 close() 在 SqlResult::close() 中调用，
 *    保证"打开了就一定会关闭"，不会资源泄漏。
 *
 * 💡 提问：为什么先 close 算子再 commit 事务？
 *    如果先 commit 再 close 会有什么问题？
 *    （提示：commit 之后数据已经持久化了，close 如果失败怎么处理？）
 */
RC SqlResult::close()
{
  if (nullptr == operator_) {
    return RC::INVALID_ARGUMENT;
  }
  RC rc = operator_->close();
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to close operator. rc=%s", strrc(rc));
  }

  operator_.reset();  // ★ 销毁算子树，释放内存

  // ★ 事务自动管理
  if (session_ && !session_->is_trx_multi_operation_mode()) {
    // 单语句模式：自动提交或回滚
    if (rc == RC::SUCCESS) {
      rc = session_->current_trx()->commit();    // 成功 → 提交
    } else {
      RC rc2 = session_->current_trx()->rollback();  // 失败 → 回滚
      if (rc2 != RC::SUCCESS) {
        LOG_PANIC("rollback failed. rc=%s", strrc(rc2));  // 回滚失败是严重错误
      }
    }
    session_->destroy_trx();  // 销毁事务对象
  }
  // 多语句事务模式：不自动处理，等用户发 COMMIT/ROLLBACK

  return rc;
}

/**
 * ★ 火山模型的核心：逐行拉取
 *
 * 每次调用 next_tuple() 返回一行数据：
 *   operator_->next()  → 算子树递归拉取
 *   operator_->current_tuple() → 获取当前行
 *
 * 当没有更多行时，operator_->next() 返回 RC::RECORD_EOF，循环结束。
 *
 * 💡 提问：为什么 next() 和 current_tuple() 要分成两个方法？
 *    为什么不设计成 next() 直接返回 Tuple*？
 *    （提示：想想 next() 的返回值 RC 既要表示"有数据"又要表示"数据本身"会不会冲突？
 *           如果某行数据为空怎么办？nullptr 是"没数据了"还是"数据为空"？）
 */
RC SqlResult::next_tuple(Tuple *&tuple)
{
  RC rc = operator_->next();
  if (rc != RC::SUCCESS) {
    return rc;
  }

  tuple = operator_->current_tuple();
  return rc;
}

/**
 * ★ 批量拉取模式（向量化执行）
 *
 * 与 next_tuple 不同，next_chunk 一次拉取一批行（Chunk），
 * 适合向量化算子（TABLE_SCAN_VEC, PROJECT_VEC 等）。
 *
 * 批量模式能减少虚函数调用次数，提高 CPU 缓存效率。
 *
 * 💡 提问：Chunk 的大小应该怎么定？太大浪费内存，太小又跟逐行模式差不多，
 *    这个 trade-off 应该考虑哪些因素？
 */
RC SqlResult::next_chunk(void *chunk)
{
  RC rc = operator_->next(chunk);
  return rc;
}

/**
 * ★ 设置物理算子
 *
 * 由 ExecuteStage 调用，把执行计划注入到结果对象中。
 * ASSERT 保证一个 SqlResult 只能设置一次算子（防止重复设置导致之前的结果丢失）。
 */
void SqlResult::set_operator(unique_ptr<PhysicalOperator> oper)
{
  ASSERT(operator_ == nullptr, "current operator is not null. Result is not closed?");
  operator_ = std::move(oper);
  operator_->tuple_schema(tuple_schema_);
}
