//
// Created by Wangyunlai on 2023/08/16.
//

/**
 * ==========================================================================
 * 【核心学习文件】LogicalPlanGenerator — 逻辑计划生成器
 * ==========================================================================
 *
 * ★ 定位: 把 Stmt（语义分析后的语句对象）转换成 LogicalOperator 树（逻辑执行计划）
 *
 * 这个文件展示了 SELECT 语句如何被"翻译"成一个算子树：
 *
 *   SELECT id, name FROM t1, t2 WHERE t1.x = t2.y AND t1.age > 20
 *                    |
 *                    v
 *   ProjectLogicalOperator(exprs=[id, name])
 *        ↑
 *   PredicateLogicalOperator(cond=t1.x=t2.y AND age>20)
 *        ↑
 *   JoinLogicalOperator  (两张表用 JOIN 连接)
 *       / \
 *   TableGet(t1)  TableGet(t2)
 *
 * ★ 核心方法: create_plan(SelectStmt)
 *
 * 构建顺序（从下往上）：
 *   1. TableGet — 为每张表创建叶子节点
 *   2. Join — 多张表用 Join 连接
 *   3. Predicate — WHERE 条件过滤
 *   4. GroupBy — GROUP BY 聚合（如果有）
 *   5. Project — 列投影（最顶层）
 *
 * ★ 设计亮点: 用指针追踪树的"插入点"
 *   last_oper 指针始终指向当前树的"挂载位置"，
 *   新的算子作为上一级的父节点插入，子节点挂在下面。
 *   这样避免了先构建完整树再重排的二次遍历。
 *
 * 💡 提问：为什么 Project 总是在最顶层？
 *   （提示：如果在 Predicate 之前做投影，会有什么问题？
 *          WHERE 子句可能引用不在 SELECT 列表中的列吗？）
 * ==========================================================================
 */

#include "sql/optimizer/logical_plan_generator.h"
#include "common/log/log.h"
#include "sql/operator/calc_logical_operator.h"
#include "sql/operator/delete_logical_operator.h"
#include "sql/operator/explain_logical_operator.h"
#include "sql/operator/insert_logical_operator.h"
#include "sql/operator/join_logical_operator.h"
#include "sql/operator/logical_operator.h"
#include "sql/operator/predicate_logical_operator.h"
#include "sql/operator/project_logical_operator.h"
#include "sql/operator/table_get_logical_operator.h"
#include "sql/operator/group_by_logical_operator.h"
#include "sql/stmt/calc_stmt.h"
#include "sql/stmt/delete_stmt.h"
#include "sql/stmt/explain_stmt.h"
#include "sql/stmt/filter_stmt.h"
#include "sql/stmt/insert_stmt.h"
#include "sql/stmt/select_stmt.h"
#include "sql/stmt/stmt.h"
#include "sql/expr/expression_iterator.h"

using namespace std;
using namespace common;

/**
 * ★ 根据 Stmt 类型分发到对应的方法
 */
RC LogicalPlanGenerator::create(Stmt *stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  RC rc = RC::SUCCESS;
  switch (stmt->type()) {
    case StmtType::CALC: {
      CalcStmt *calc_stmt = static_cast<CalcStmt *>(stmt);
      rc = create_plan(calc_stmt, logical_operator);
    } break;
    case StmtType::SELECT: {
      SelectStmt *select_stmt = static_cast<SelectStmt *>(stmt);
      rc = create_plan(select_stmt, logical_operator); // ★ 核心方法
    } break;
    case StmtType::INSERT: {
      InsertStmt *insert_stmt = static_cast<InsertStmt *>(stmt);
      rc = create_plan(insert_stmt, logical_operator);
    } break;
    case StmtType::DELETE: {
      DeleteStmt *delete_stmt = static_cast<DeleteStmt *>(stmt);
      rc = create_plan(delete_stmt, logical_operator);
    } break;
    case StmtType::EXPLAIN: {
      ExplainStmt *explain_stmt = static_cast<ExplainStmt *>(stmt);
      rc = create_plan(explain_stmt, logical_operator);
    } break;
    default: {
      rc = RC::UNIMPLEMENTED;
    }
  }
  return rc;
}

/**
 * ★★★ SELECT 逻辑计划生成 ★★★
 *
 * 这是最重要的方法。以 SELECT id, name FROM t1 WHERE id > 10 为例：
 *
 * 输入: SelectStmt { tables=[t1], filter=[id>10], expressions=[id, name] }
 *
 * 构建过程:
 *   Step 1: TableGetLogicalOperator(t1)           — 叶子节点
 *   Step 2: PredicateLogicalOperator(id>10)       — 中间节点（WHERE过滤）
 *           → 挂在 TableGet 上面
 *   Step 3: ProjectLogicalOperator([id, name])    — 根节点（列投影）
 *           → 挂在 Predicate 上面
 *
 * 输出: Project → Predicate → TableGet 的树
 *
 * ★ last_oper 指针的作用:
 *   始终指向当前树的最底层（即下一个算子应该挂在谁上面）。
 *   每次插入新算子时:
 *     1. 把 last_oper 作为新算子的子节点
 *     2. 把 last_oper 更新为新算子
 *   这样新算子就"包裹"在了 old_last_oper 外面。
 *
 * 💡 提问：多表查询时（如 FROM t1, t2），TableGet 怎么处理？
 *   看 for(Table *table : tables) 循环：第一张表创建 TableGet，
 *   后续表创建 Join 节点，Join 的左右子节点分别是之前的树和新表的 TableGet。
 *
 * 💡 提问：如果 WHERE 条件引用了索引列，优化器能把 TableScan 换成 IndexScan 吗？
 *   在这里看不到 IndexScan 的创建 — 这个优化在 physical_plan_generator 阶段做，
 *   这体现了"逻辑计划"和"物理计划"的分离。
 */
RC LogicalPlanGenerator::create_plan(SelectStmt *select_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  unique_ptr<LogicalOperator> *last_oper = nullptr;

  unique_ptr<LogicalOperator> table_oper(nullptr);
  last_oper = &table_oper;

  // ★ Step 1: 创建表访问节点（叶子层）
  // 一个表 → TableGet；多个表 → Join(TableGet(t1), TableGet(t2), ...)
  const vector<Table *> &tables = select_stmt->tables();
  for (Table *table : tables) {
    unique_ptr<LogicalOperator> table_get_oper(
        new TableGetLogicalOperator(table, ReadWriteMode::READ_ONLY));

    if (table_oper == nullptr) {
      table_oper = std::move(table_get_oper);  // 第一张表
    } else {
      // ★ 多表：用 Join 连接已构建的树和新表
      JoinLogicalOperator *join_oper = new JoinLogicalOperator;
      join_oper->add_child(std::move(table_oper));
      join_oper->add_child(std::move(table_get_oper));
      table_oper = unique_ptr<LogicalOperator>(join_oper);
    }
  }

  // ★ Step 2: 创建过滤节点（Predicate层，WHERE 条件）
  unique_ptr<LogicalOperator> predicate_oper;
  RC rc = create_plan(select_stmt->filter_stmt(), predicate_oper);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to create predicate logical plan. rc=%s", strrc(rc));
    return rc;
  }

  if (predicate_oper) {
    // ★ 把 TableGet/Join 挂在 Predicate 下面
    if (*last_oper) {
      predicate_oper->add_child(std::move(*last_oper));
    }
    last_oper = &predicate_oper;
  }

  // ★ Step 3: 创建 GROUP BY 节点（如果有聚合）
  unique_ptr<LogicalOperator> group_by_oper;
  rc = create_group_by_plan(select_stmt, group_by_oper);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to create group by logical plan. rc=%s", strrc(rc));
    return rc;
  }
  if (group_by_oper) {
    if (*last_oper) {
      group_by_oper->add_child(std::move(*last_oper));
    }
    last_oper = &group_by_oper;
  }

  // ★ Step 4: 创建投影节点（Projection层，SELECT 列）
  unique_ptr<LogicalOperator> project_oper = make_unique<ProjectLogicalOperator>(
      std::move(select_stmt->query_expressions()));

  if (*last_oper) {
    project_oper->add_child(std::move(*last_oper));
  }
  last_oper = &project_oper;

  // ★ 最终：last_oper 指向树根（Project）
  logical_operator = std::move(*last_oper);
  return RC::SUCCESS;
}

/**
 * ★ 创建 WHERE 子句的过滤节点
 *
 * filter_stmt 包含一组 FilterUnit，每个是一个比较条件。
 * 所有 FilterUnit 以 AND 方式连接（CombinationExpr::AND），
 * 生成一个 PredicateLogicalOperator。
 *
 * filter_stmt 为 nullptr 或 filter_units 为空时，没有过滤条件，
 * predicate_oper 保持 nullptr — 表示不需要过滤。
 *
 * ★ 隐式类型转换:
 *   如果比较的两边类型不匹配（如 INT vs FLOAT），
 *   会计算转换代价，选择代价更低的方向做隐式转换。
 *   这允许 "WHERE id > 3.14" 正常工作。
 *
 * 💡 提问：为什么 WHERE 条件用 AND 连接多个 FilterUnit？
 *   如果条件是 "(a>10 OR b<20) AND c=30" 怎么办？
 *   （提示：这个语法目前不支持 OR，所有条件默认 AND 连接）
 */
RC LogicalPlanGenerator::create_plan(FilterStmt *filter_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  // ...（FilterUnit → ComparisonExpr → PredicateLogicalOperator）
  return RC::SUCCESS;
}

/**
 * ★ INSERT 逻辑计划
 *
 * INSERT 语句的逻辑计划很简单：只有一个 InsertLogicalOperator，
 * 包含目标表和要插入的值。没有 TableScan/Project 这些，
 * 因为 INSERT 不需要"读数据 → 过滤 → 投影"，直接写数据即可。
 */
RC LogicalPlanGenerator::create_plan(InsertStmt *insert_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  Table        *table = insert_stmt->table();
  vector<Value> values(insert_stmt->values(), insert_stmt->values() + insert_stmt->value_amount());
  InsertLogicalOperator *insert_operator = new InsertLogicalOperator(table, values);
  logical_operator.reset(insert_operator);
  return RC::SUCCESS;
}

/**
 * ★ DELETE 逻辑计划
 *
 * DELETE 的算子树结构与 SELECT 类似，但根节点不同：
 *   DeleteLogicalOperator
 *        ↑
 *   PredicateLogicalOperator (WHERE 条件过滤)
 *        ↑
 *   TableGetLogicalOperator
 *
 * 与 SELECT 的区别：顶层是 Delete 而不是 Project（不需要输出列）
 */
RC LogicalPlanGenerator::create_plan(DeleteStmt *delete_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  Table                      *table       = delete_stmt->table();
  FilterStmt                 *filter_stmt = delete_stmt->filter_stmt();
  unique_ptr<LogicalOperator> table_get_oper(new TableGetLogicalOperator(table, ReadWriteMode::READ_WRITE));
  unique_ptr<LogicalOperator> predicate_oper;

  RC rc = create_plan(filter_stmt, predicate_oper);
  if (rc != RC::SUCCESS) { return rc; }

  unique_ptr<LogicalOperator> delete_oper(new DeleteLogicalOperator(table));

  if (predicate_oper) {
    predicate_oper->add_child(std::move(table_get_oper));
    delete_oper->add_child(std::move(predicate_oper));
  } else {
    delete_oper->add_child(std::move(table_get_oper));
  }

  logical_operator = std::move(delete_oper);
  return rc;
}

/**
 * ★ GROUP BY 逻辑计划
 *
 * 处理聚合查询（配合聚合函数 COUNT/SUM/AVG 等）：
 *   1. 遍历查询表达式，收集所有聚合表达式
 *   2. 检查非聚合列是否在 GROUP BY 中
 *   3. 创建 GroupByLogicalOperator
 *
 * 例如 SELECT city, COUNT(*) FROM t1 GROUP BY city：
 *   group_by_expressions = [FieldExpr(city)]
 *   aggregate_expressions = [AggregateExpr(COUNT, StarExpr)]
 *
 * ★ 确保 SQL 语义正确：
 *   如果 SELECT 中有非聚合列但不在 GROUP BY 中，报错。
 *   这符合标准 SQL 的约束：SELECT 中的列要么在 GROUP BY 中，要么是聚合函数。
 */
RC LogicalPlanGenerator::create_group_by_plan(SelectStmt *select_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  // ...（表达式遍历 → 收集聚合 → 验证约束 → 生成 GroupByLogicalOperator）
  return RC::SUCCESS;
}
