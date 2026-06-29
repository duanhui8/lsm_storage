/* Copyright (c) 2023 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Wangyunlai on 2023/08/16.
//

/**
 * ==========================================================================
 * ★★★ LogicalPlanGenerator — 逻辑计划生成器 ★★★
 * ==========================================================================
 *
 * ★ 文件定位：
 *   将 Stmt（语义对象）翻译成 LogicalOperator（逻辑计划树）。
 *   这是 5 阶段流水线中 Optimize 阶段的第一步：create_logical_plan。
 *
 * ★ 核心流程（以 SELECT 为例）：
 *   SELECT a, b FROM t1, t2 WHERE t1.x = t2.y GROUP BY a
 *         │
 *         ▼
 *   GroupBy(a)                                  ← 分组聚合
 *       │
 *   Project(a, b)                               ← 投影（输出列）
 *       │
 *   Filter(t1.x = t2.y)                         ← 过滤条件
 *       │
 *   Join                                         ← 表连接
 *      /  \
 *   Scan(t1)  Scan(t2)                          ← 表访问
 *
 * ★ 自底向上构建（看 create_plan(SelectStmt)）：
 *   1. TableGet（最底层叶子）→ 读表数据
 *   2. Join（多表时）→ 连接表
 *   3. Filter/Predicate → 应用 WHERE 条件
 *   4. GroupBy → 分组+聚合
 *   5. Project（最顶层根）→ 选择输出列
 *
 * ★ 为什么叫"逻辑"计划？
 *   因为树中的每个节点只描述"做什么操作"，不绑定"怎么做"。
 *   比如 TableGet 只说了"读表 t1"，没说是全表扫还是索引扫——
 *   那是物理计划 genrator 的职责。
 *
 * 💡 提问：为什么 create_plan(SelectStmt) 用 last_oper 指针来"穿线"？
 *   （提示：构建过程自底向上，每加一层就把前一层的根作为子节点。
 *         last_oper 始终指向"当前树的根"，新层挂到它上面。
 *         这避免了在构建过程中需要知道树的完整结构）
 *
 * 💡 提问：为什么隐式类型转换（implicit_cast_cost）要在逻辑计划阶段做？
 *   （提示：WHERE int_col = '10'，'10' 是字符串，int_col 是整数。
 *          需要决定谁转谁。这个决策跟"逻辑语义"相关，
 *          不依赖具体算子选择，所以放在逻辑计划阶段而非物理阶段）
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
#include "common/types.h"

#include "sql/expr/expression_iterator.h"

using namespace std;
using namespace common;

/**
 * ★★★ create — 工厂方法，按 Stmt 类型分发 ★★★
 *
 * 这是逻辑计划生成的入口。根据 Stmt 的具体类型，
 * 调用对应的 create_plan 重载版本。
 *
 * 调用链：OptimizeStage → LogicalPlanGenerator::create → create_plan(XXXStmt)
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
      rc = create_plan(select_stmt, logical_operator);
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
 * ★ create_plan(CalcStmt) — CALC 语句
 *
 * CALC 是最简单的语句类型（如 "CALC 1+2"），
 * 没有表、没有过滤，只有一个表达式列表。
 * 直接创建 CalcLogicalOperator，不做任何子节点。
 */
RC LogicalPlanGenerator::create_plan(CalcStmt *calc_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  logical_operator.reset(new CalcLogicalOperator(std::move(calc_stmt->expressions())));
  return RC::SUCCESS;
}

/**
 * ★★★ create_plan(SelectStmt) — SELECT 语句（最复杂的逻辑计划构建）★★★
 *
 * 自底向上的构建过程：
 *   扫描表 → 连接 → 过滤 → 分组 → 投影
 *
 * 使用 last_oper 指针来"穿线"：
 *   每构建一层，就把该层设为新的根，下一层挂为子节点。
 *   outer_oper 像一个"指针"，总是指向当前树的根。
 *
 * 示例（SELECT a FROM t1, t2 WHERE t1.x > 10）：
 *   步骤1: table_oper = Join(Scan(t1), Scan(t2))
 *   步骤2: predicate_oper = Filter(x>10)，把 table_oper 挂为子节点
 *   步骤3: project_oper = Project(a)，把 predicate_oper 挂为子节点
 *   最终: Project → Filter → Join → [Scan, Scan]
 */
RC LogicalPlanGenerator::create_plan(SelectStmt *select_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  unique_ptr<LogicalOperator> *last_oper = nullptr;

  // ★ 步骤1: 构建表扫描/连接子树
  unique_ptr<LogicalOperator> table_oper(nullptr);
  last_oper = &table_oper;
  unique_ptr<LogicalOperator> predicate_oper;

  // ★ 步骤1a: 先构建 WHERE 条件的子树（后面再挂到 table 子树上）
  RC rc = create_plan(select_stmt->filter_stmt(), predicate_oper);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to create predicate logical plan. rc=%s", strrc(rc));
    return rc;
  }

  // ★ 步骤1b: 为每张表创建 TableGet 节点，多表时用 Join 连接
  const auto &tables = select_stmt->tables();
  if (tables.empty()) { return RC::SUCCESS; }
  int idx = 0;
  for (auto *table : tables) {
    auto *tgl = new TableGetLogicalOperator(table, ReadWriteMode::READ_ONLY);
    if (idx < static_cast<int>(select_stmt->table_names().size()))
      tgl->set_table_name(select_stmt->table_names()[idx]);
    unique_ptr<LogicalOperator> table_get_oper(tgl);
    if (table_oper == nullptr) {
      table_oper = std::move(table_get_oper);
      idx++;
    } else {
      JoinLogicalOperator *join_oper = new JoinLogicalOperator;   // ★ 后续表通过 Join 连接
      join_oper->add_child(std::move(table_oper));
      join_oper->add_child(std::move(table_get_oper));
      table_oper = unique_ptr<LogicalOperator>(join_oper);
    }
  }

  // ★ 步骤2: 将 Filter 挂在 table 子树上
  if (predicate_oper) {
    if (*last_oper) {
      predicate_oper->add_child(std::move(*last_oper));
    }
    last_oper = &predicate_oper;
  }

  // ★ 步骤3: 构建 GROUP BY 子树
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

  // ★ 步骤4: 构建 PROJECT（最顶层——选择输出列）
  unique_ptr<LogicalOperator> project_oper = make_unique<ProjectLogicalOperator>(
      std::move(select_stmt->query_expressions()));
  if (*last_oper) {
    project_oper->add_child(std::move(*last_oper));
  }

  last_oper = &project_oper;

  logical_operator = std::move(*last_oper);
  return RC::SUCCESS;
}

/**
 * ★★★ create_plan(FilterStmt) — 构建 WHERE 条件子树 ★★★
 *
 * FilterStmt 中有一组 FilterUnit，每个对应一个比较条件（如 t1.x > 10）。
 * 多个 FilterUnit 之间是 AND 关系。
 *
 * 流程：
 *   1. 遍历每个 FilterUnit，创建 ComparisonExpr（左边 op 右边）
 *   2. 先做隐式类型转换（如 int_col = '10' → int_col = 10）
 *   3. 把 ValueExpr 常量折叠（cast_expr->try_get_value）
 *   4. 所有 ComparisonExpr 用 ConjunctionExpr(AND) 包起来
 *   5. 整体放入 PredicateLogicalOperator
 *
 * 💡 提问：为什么在 Filter 构建时就做类型转换和常量折叠，
 *         而不是在 Rewrite 阶段统一处理？
 *   （提示：这里的是"必要"的转换——不转换就无法正确执行。
 *         比如 int_col = '10'，如果不把 '10' 转为整数，
 *         类型不匹配会导致比较出错。Rewrite 阶段做的是"优化"性转换）
 */
RC LogicalPlanGenerator::create_plan(FilterStmt *filter_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  if (filter_stmt == nullptr) return RC::SUCCESS;
  RC                                  rc = RC::SUCCESS;
  vector<unique_ptr<Expression>> cmp_exprs;
  const vector<FilterUnit *>    &filter_units = filter_stmt->filter_units();
  for (const FilterUnit *filter_unit : filter_units) {
    const FilterObj &filter_obj_left  = filter_unit->left();
    const FilterObj &filter_obj_right = filter_unit->right();

    // ★ 将 FilterObj 转为 Expression：字段引用 → FieldExpr，字面值 → ValueExpr
    unique_ptr<Expression> left(filter_obj_left.is_attr
                                    ? static_cast<Expression *>(new FieldExpr("", "", 0, AttrType::UNDEFINED, 0))
                                    : static_cast<Expression *>(new ValueExpr(filter_obj_left.value)));

    unique_ptr<Expression> right(filter_obj_right.is_attr
                                     ? static_cast<Expression *>(new FieldExpr("", "", 0, AttrType::UNDEFINED, 0))
                                     : static_cast<Expression *>(new ValueExpr(filter_obj_right.value)));

    // ★ 隐式类型转换：两边类型不一致时，选择代价更小的转换方向
    if (left->value_type() != right->value_type()) {
      auto left_to_right_cost = implicit_cast_cost(left->value_type(), right->value_type());
      auto right_to_left_cost = implicit_cast_cost(right->value_type(), left->value_type());
      if (left_to_right_cost <= right_to_left_cost && left_to_right_cost != INT32_MAX) {
        // ★ 把 left 转为 right 的类型
        ExprType left_type = left->type();
        auto cast_expr = make_unique<CastExpr>(std::move(left), right->value_type());
        if (left_type == ExprType::VALUE) {
          // ★ 常量可以直接算出转换后的值（常量折叠）
          Value left_val;
          if (OB_FAIL(rc = cast_expr->try_get_value(left_val))) {
            LOG_WARN("failed to get value from left child", strrc(rc));
            return rc;
          }
          left = make_unique<ValueExpr>(left_val);
        } else {
          left = std::move(cast_expr);
        }
      } else if (right_to_left_cost < left_to_right_cost && right_to_left_cost != INT32_MAX) {
        // ★ 把 right 转为 left 的类型
        ExprType right_type = right->type();
        auto cast_expr = make_unique<CastExpr>(std::move(right), left->value_type());
        if (right_type == ExprType::VALUE) {
          Value right_val;
          if (OB_FAIL(rc = cast_expr->try_get_value(right_val))) {
            LOG_WARN("failed to get value from right child", strrc(rc));
            return rc;
          }
          right = make_unique<ValueExpr>(right_val);
        } else {
          right = std::move(cast_expr);
        }
      } else {
        rc = RC::UNSUPPORTED;
        LOG_WARN("unsupported cast from %s to %s",
                 attr_type_to_string(left->value_type()), attr_type_to_string(right->value_type()));
        return rc;
      }
    }

    ComparisonExpr *cmp_expr = new ComparisonExpr(filter_unit->comp(), std::move(left), std::move(right));
    cmp_exprs.emplace_back(cmp_expr);
  }

  unique_ptr<PredicateLogicalOperator> predicate_oper;
  if (!cmp_exprs.empty()) {
    // ★ 所有比较条件用 AND 连接（MiniOB 不支持 OR 下推，所以全部是 AND）
    unique_ptr<ConjunctionExpr> conjunction_expr(new ConjunctionExpr(ConjunctionExpr::Type::AND, cmp_exprs));
    predicate_oper = unique_ptr<PredicateLogicalOperator>(
        new PredicateLogicalOperator(std::move(conjunction_expr)));
  }

  logical_operator = std::move(predicate_oper);
  return rc;
}

/**
 * ★ implicit_cast_cost — 计算隐式类型转换的代价
 *
 * 值越小表示转换越"自然"（如 INT→FLOAT 代价低，INT→TEXT 代价高）。
 * 返回 INT32_MAX 表示不能转换。
 * 用于在多个可能的转换方向中选择代价更小的那个。
 */
int LogicalPlanGenerator::implicit_cast_cost(AttrType from, AttrType to)
{
  if (from == to) {
    return 0;
  }
  return DataType::type_instance(from)->cast_cost(to);
}

/**
 * ★ create_plan(InsertStmt) — INSERT 语句
 *
 * 结构简单：直接创建一个 InsertLogicalOperator，
 * 包含目标表和要插入的值列表。不需要子节点。
 */
RC LogicalPlanGenerator::create_plan(InsertStmt *insert_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  auto         *table = insert_stmt->table();
  vector<Value> values(insert_stmt->values(), insert_stmt->values() + insert_stmt->value_amount());

  InsertLogicalOperator *insert_operator = new InsertLogicalOperator(table, values);
  logical_operator.reset(insert_operator);
  return RC::SUCCESS;
}

/**
 * ★ create_plan(DeleteStmt) — DELETE 语句
 *
 * 结构：Delete → [Filter → [TableGet]]
 *
 * DELETE 需要 READ_WRITE 模式（实际写数据），
 * 而 SELECT 使用 READ_ONLY。
 *
 * 如果有 WHERE 条件，Filter 放在 TableGet 上面做过滤。
 */
RC LogicalPlanGenerator::create_plan(DeleteStmt *delete_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  auto                       *table       = delete_stmt->table();
  FilterStmt                 *filter_stmt = delete_stmt->filter_stmt();
  unique_ptr<LogicalOperator> table_get_oper(new TableGetLogicalOperator(table, ReadWriteMode::READ_WRITE));

  unique_ptr<LogicalOperator> predicate_oper;

  RC rc = create_plan(filter_stmt, predicate_oper);
  if (rc != RC::SUCCESS) {
    return rc;
  }

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
 * ★ create_plan(ExplainStmt) — EXPLAIN 语句
 *
 * EXPLAIN 只是一个包装器：它有一个子计划（真正要执行的查询），
 * EXPLAIN 本身不执行子计划，而是把子计划的结构展示给用户。
 */
RC LogicalPlanGenerator::create_plan(ExplainStmt *explain_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  unique_ptr<LogicalOperator> child_oper;

  Stmt *child_stmt = explain_stmt->child();

  RC rc = create(child_stmt, child_oper);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to create explain's child operator. rc=%s", strrc(rc));
    return rc;
  }

  logical_operator = unique_ptr<LogicalOperator>(new ExplainLogicalOperator);
  logical_operator->add_child(std::move(child_oper));
  return rc;
}

/**
 * ★★★ create_group_by_plan — 构建 GROUP BY 子树 ★★★
 *
 * 这是逻辑计划生成中最复杂的部分，处理 SQL 的 GROUP BY 和聚合函数。
 *
 * 关键数据结构：
 *   - group_by_expressions：GROUP BY 子句中的列
 *   - aggregate_expressions：聚合函数（SUM, COUNT, AVG, MAX, MIN）
 *   - Expression::pos_：表达式在输出中的位置编号
 *
 * 三个核心步骤：
 *
 *   步骤1 (bind_group_by_expr)：绑定引用关系
 *     遍历 query_expressions 中的每个表达式，检查它是否引用了 group by 中的列。
 *     如果是，设置 expr->set_pos(i) 标记它对应 group_by[i]。
 *
 *   步骤2 (find_unbound_column)：检查是否有未绑定列
 *     如果 SELECT 中出现了既不在 GROUP BY 中也不是聚合函数的列，
 *     报错 "column must appear in the GROUP BY clause"。
 *
 *   步骤3 (collector)：收集所有聚合表达式
 *     遍历 query_expressions，收集所有 AGGREGATION 类型的表达式，
 *     设置位置编号（group_by 列 + 聚合函数在输出中的序号）。
 *
 * 示例：SELECT a, SUM(b) FROM t GROUP BY a
 *   group_by_expressions = [a]
 *   aggregate_expressions = [SUM(b)]
 *   输出：pos[0]=a(gby), pos[1]=SUM(b)(agg)
 *
 * 💡 提问：为什么 GROUP BY 要在逻辑计划阶段就处理，
 *         而不是等到物理计划再决定怎么分组？
 *   （提示：GROUP BY 改变了查询的"语义"——从行级操作变为分组操作。
 *          这不是"怎么做"的问题，而是"做什么"的问题。
 *          有没有 GROUP BY 会导致完全不同的输出行数，
 *          这是逻辑层面的变化，必须在逻辑计划中体现）
 */
RC LogicalPlanGenerator::create_group_by_plan(SelectStmt *select_stmt, unique_ptr<LogicalOperator> &logical_operator)
{
  vector<unique_ptr<Expression>> &group_by_expressions = select_stmt->group_by();
  vector<Expression *> aggregate_expressions;
  vector<unique_ptr<Expression>> &query_expressions = select_stmt->query_expressions();

  // ★ 步骤3: 收集所有聚合表达式（SUM/COUNT/AVG/MAX/MIN）
  function<RC(unique_ptr<Expression>&)> collector = [&](unique_ptr<Expression> &expr) -> RC {
    RC rc = RC::SUCCESS;
    if (expr->type() == ExprType::AGGREGATION) {
      // ★ 聚合表达式的位置 = GROUP BY 列数 + 聚合函数序号
      expr->set_pos(aggregate_expressions.size() + group_by_expressions.size());
      aggregate_expressions.push_back(expr.get());
    }
    rc = ExpressionIterator::iterate_child_expr(*expr, collector);
    return rc;
  };

  // ★ 步骤1: 绑定——将 query 表达式关联到 group_by 列
  function<RC(unique_ptr<Expression>&)> bind_group_by_expr = [&](unique_ptr<Expression> &expr) -> RC {
    RC rc = RC::SUCCESS;
    for (size_t i = 0; i < group_by_expressions.size(); i++) {
      auto &group_by = group_by_expressions[i];
      if (expr->type() == ExprType::AGGREGATION) {
        break;  // ★ 聚合函数内部不需要绑定（SUM(b) 中的 b 不是 GROUP BY 列）
      } else if (expr->equal(*group_by)) {
        expr->set_pos(i);  // ★ 标记：该表达式引用 group_by 的第 i 列
        continue;
      } else {
        rc = ExpressionIterator::iterate_child_expr(*expr, bind_group_by_expr);
      }
    }
    return rc;
  };

  // ★ 步骤2: 检查是否有未绑定列——违反 SQL 语义
  bool found_unbound_column = false;
  function<RC(unique_ptr<Expression>&)> find_unbound_column = [&](unique_ptr<Expression> &expr) -> RC {
    RC rc = RC::SUCCESS;
    if (expr->type() == ExprType::AGGREGATION) {
      // ★ 聚合函数内部不检查（SUM 的参数不需要出现在 GROUP BY 中）
    } else if (expr->pos() != -1) {
      // ★ 已绑定（是 GROUP BY 列或聚合结果）
    } else if (expr->type() == ExprType::FIELD) {
      found_unbound_column = true;  // ★ 列既不在 GROUP BY 中，也不是聚合参数 → 错误
    } else {
      rc = ExpressionIterator::iterate_child_expr(*expr, find_unbound_column);
    }
    return rc;
  };

  // 执行步骤1：绑定
  for (unique_ptr<Expression> &expression : query_expressions) {
    bind_group_by_expr(expression);
  }

  // 执行步骤2：检查未绑定列
  for (unique_ptr<Expression> &expression : query_expressions) {
    find_unbound_column(expression);
  }

  // 执行步骤3：收集聚合表达式
  for (unique_ptr<Expression> &expression : query_expressions) {
    collector(expression);
  }

  // ★ 没有任何 GROUP BY 或聚合函数 → 不需要 GroupBy 算子
  if (group_by_expressions.empty() && aggregate_expressions.empty()) {
    return RC::SUCCESS;
  }

  // ★ 有未绑定列 → 报错
  if (found_unbound_column) {
    LOG_WARN("column must appear in the GROUP BY clause or must be part of an aggregate function");
    return RC::INVALID_ARGUMENT;
  }

  auto group_by_oper = make_unique<GroupByLogicalOperator>(std::move(group_by_expressions),
                                                           std::move(aggregate_expressions));
  logical_operator = std::move(group_by_oper);
  return RC::SUCCESS;
}
