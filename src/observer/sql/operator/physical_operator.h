//
// Created by WangYunlai on 2022/6/7.
//

#pragma once

/**
 * ==========================================================================
 * 【架构概览】PhysicalOperator — 火山模型算子基类
 * ==========================================================================
 *
 * 这个类是 MiniOB 执行引擎的基石。所有具体的执行操作都继承自这个类。
 *
 * ★ 核心设计模式：火山模型（Volcano Model / Iterator Model）
 *
 *   每个算子实现三个核心方法，形成"迭代器"接口：
 *     open(trx)  — 初始化，准备资源（打开表、分配内存、启动事务）
 *     next()     — 获取下一行/下一批（核心：递归调用子算子的 next）
 *     close()    — 清理资源（关闭表、释放内存）
 *
 *   调用方式是一个"拉取"循环：
 *     operator->open(trx);
 *     while (operator->next() == SUCCESS) {
 *         tuple = operator->current_tuple();
 *         // 处理这一行...
 *     }
 *     operator->close();
 *
 * ★ 算子树的递归结构：
 *
 *   每个算子有 children_（子算子列表），形成一个树：
 *
 *         Project（根：投影列）
 *           │
 *         Predicate（过滤 WHERE 条件）
 *           │
 *         TableScan（叶子：读表数据）
 *
 *   当调用 Project::next() 时，它会调用 Predicate::next()，
 *   Predicate 又调用 TableScan::next()，层层递归，直到叶子节点从存储引擎读取数据。
 *
 * ★ 设计亮点：多子节点支持
 *   children_ 是一个 vector，可以放多个子算子。
 *   单输入算子（Project、Predicate）只有 1 个子节点，
 *   多输入算子（Join）有 2 个子节点（左表、右表），
 *   无输入算子（TableScan）没有子节点（叶子节点）。
 *
 * ★ 扩展点：向量化执行
 *   除了传统的 next() → 返回单行，还提供了 next(void *chunk) → 批量返回。
 *   对应的向量化算子类型有 TABLE_SCAN_VEC、PROJECT_VEC、GROUP_BY_VEC 等。
 *
 * 💡 提问：为什么用继承（虚函数多态）而不是 std::function 或模板来实现算子？
 *    （提示：考虑不同类型的算子如何存储在同一个容器里？执行时如何统一调用？）
 *
 * 💡 提问：火山模型的"每次 next 只返回一行"有什么缺点？
 *    大数据量场景下，如果表有 1 亿行，需要调用 1 亿次虚函数，
 *    虚函数调用的开销有多大？怎么优化？
 * ==========================================================================
 */

#include "common/sys/rc.h"
#include "sql/expr/tuple.h"
#include "sql/operator/operator_node.h"

class Record;
class TupleCellSpec;
class Trx;

/**
 * @brief 物理算子类型枚举
 * 每个枚举值对应一种具体的执行操作
 *
 * ★ 学习提示：从 TABLE_SCAN → PREDICATE → PROJECT 追踪一条 SELECT 的执行路径
 */
enum class PhysicalOperatorType
{
  TABLE_SCAN,         // 全表扫描（逐行）
  TABLE_SCAN_VEC,     // 全表扫描（向量化/批量）
  INDEX_SCAN,         // 索引扫描
  NESTED_LOOP_JOIN,   // 嵌套循环连接（两表关联）
  HASH_JOIN,          // 哈希连接
  EXPLAIN,            // 查看执行计划
  PREDICATE,          // 谓词过滤（执行 WHERE 条件）
  PREDICATE_VEC,      // 谓词过滤（向量化）
  PROJECT,            // 投影（选择需要的列）
  PROJECT_VEC,        // 投影（向量化）
  CALC,               // 表达式计算
  STRING_LIST,        // 字符串列表
  DELETE,             // 删除
  INSERT,             // 插入
  SCALAR_GROUP_BY,    // 标量聚合（GROUP BY）
  HASH_GROUP_BY,      // 哈希聚合（GROUP BY）
  GROUP_BY_VEC,       // 哈希聚合（向量化）
  AGGREGATE_VEC,      // 聚合函数（向量化）
  EXPR_VEC,           // 向量化表达式
};

/**
 * @brief 物理算子基类
 *
 * ★ 继承体系：
 *   OperatorNode（基类，提供树结构的 children_ 管理）
 *     → PhysicalOperator（本类，定义 open/next/close 接口）
 *       → TableScanPhysicalOperator, PredicatePhysicalOperator, ...
 */
class PhysicalOperator : public OperatorNode
{
public:
  PhysicalOperator() = default;

  virtual ~PhysicalOperator() = default;

  virtual string name() const;
  virtual string param() const;

  bool is_physical() const override { return true; }
  bool is_logical() const override { return false; }

  virtual PhysicalOperatorType type() const = 0;

  /**
   * ★ 初始化算子
   * @param trx 当前事务对象，算子通过它来读取数据（保证事务隔离性）
   */
  virtual RC open(Trx *trx) = 0;

  /**
   * ★ 获取下一行
   * @return RC::SUCCESS 拿到一行，RC::RECORD_EOF 没数据了，其他值表示出错
   *
   * 默认返回 UNIMPLEMENTED，叶子算子必须重写此方法。
   */
  virtual RC next() { return RC::UNIMPLEMENTED; }

  /**
   * ★ 批量获取下一批数据（向量化执行路径）
   * @param chunk 输出参数，存放一批行数据
   * @return RC::SUCCESS 拿到一批，RC::RECORD_EOF 没数据了
   *
   * 默认返回 UNIMPLEMENTED，只有向量化算子重写此方法。
   */
  virtual RC next(void *chunk) { return RC::UNIMPLEMENTED; }

  /**
   * ★ 关闭算子、释放资源
   */
  virtual RC close() = 0;

  /**
   * ★ 获取当前行
   * 为什么需要单独的方法？因为 next() 和 current_tuple() 各司其职：
   *   next()：推进迭代器到下一行（有副作用）
   *   current_tuple()：只看当前行（无副作用，可多次调用）
   */
  virtual Tuple *current_tuple() { return nullptr; }

  virtual RC tuple_schema(TupleSchema &schema) const { return RC::UNIMPLEMENTED; }

  /**
   * ★ 添加子算子
   * 子算子的生命周期由父算子管理（unique_ptr 独占所有权）
   */
  void add_child(unique_ptr<PhysicalOperator> oper) { children_.emplace_back(std::move(oper)); }

  vector<unique_ptr<PhysicalOperator>> &children() { return children_; }

protected:
  vector<unique_ptr<PhysicalOperator>> children_;  /// ★ 子算子列表（核心：树结构的关键）
};
