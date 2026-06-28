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
// Created by Wangyunlai on 2022/12/08.
//

/**
 * ==========================================================================
 * ★★★ LogicalOperator — 逻辑计划算子基类 ★★★
 * ==========================================================================
 *
 * ★ 文件定位：
 *   LogicalOperator 是所有逻辑计划节点的基类。
 *   它不包含任何具体操作的实现，只定义了"逻辑算子的共性"：
 *   1. 树结构（父子关系）：add_child / children_
 *   2. 关联表达式：add_expressions / expressions_
 *   3. 向量化支持判断：can_generate_vectorized_operator
 *
 * ★ 逻辑算子 vs 物理算子的区别：
 *   LogicalOperator：描述"做什么"（抽象关系代数操作）
 *     - TableGet：读表
 *     - Predicate：过滤
 *     - Project：投影
 *     - Join：连接
 *     - GroupBy：分组
 *     - Insert/Delete/Explain
 *
 *   PhysicalOperator：描述"怎么做"（绑定具体算法）
 *     - 同一个 TableGet 可能变成 TableScan 或 IndexScan
 *     - 同一个 Join 可能变成 NestedLoopJoin / HashJoin
 *     - 同一个 GroupBy 可能变成 HashGroupBy / ScalarGroupBy
 *
 * ★ children_ vs general_children_ 的区别：
 *   - children_：原始的智能指针子节点列表（拥有所有权）
 *   - general_children_：原始指针列表（不拥有所有权，遍历方便）
 *   generate_general_child() 从 children_ 生成 general_children_
 *
 * ★ can_generate_vectorized_operator：
 *   判断该逻辑算子是否支持向量化执行。
 *   CALC / DELETE / INSERT 目前不支持向量化，其他都支持。
 *
 * 💡 提问：为什么需要 children_（unique_ptr）和 general_children_（裸指针）两套？
 *   （提示：unique_ptr 保证了所有权的清晰——谁持有谁负责释放。
 *         general_children_ 是给遍历/访问用的便利列表，
 *         不需要每次访问都从 unique_ptr 取 .get()，
 *         也避免了频繁移动 unique_ptr 所有权的开销）
 * ==========================================================================
 */

#include "sql/operator/logical_operator.h"

LogicalOperator::~LogicalOperator() {}

/**
 * ★ add_child — 添加子节点
 *
 * 使用 std::move 转移所有权：调用者交出子节点的所有权给当前节点。
 * 之后调用者不能再使用该子节点。
 */
void LogicalOperator::add_child(unique_ptr<LogicalOperator> oper)
{
  children_.emplace_back(std::move(oper));
}

void LogicalOperator::add_expressions(unique_ptr<Expression> expr)
{
  expressions_.emplace_back(std::move(expr));
}

/**
 * ★ can_generate_vectorized_operator — 判断算子是否支持向量化执行
 *
 * 向量化执行（一次处理一批数据，而非逐行）可以大幅提升 OLAP 性能。
 * 但不是所有算子都能向量化——INSERT/DELETE/CALC 当前不支持。
 */
bool LogicalOperator::can_generate_vectorized_operator(const LogicalOperatorType &type)
{
  bool bool_ret = false;
  switch (type)
  {
  case LogicalOperatorType::CALC:
  case LogicalOperatorType::DELETE:
  case LogicalOperatorType::INSERT:
    bool_ret = false;
    break;

  default:
    bool_ret = true;
    break;
  }
  return bool_ret;
}

/**
 * ★ generate_general_child — 生成裸指针子节点列表
 *
 * 从 children_（unique_ptr）提取原始指针到 general_children_。
 * 递归处理整棵树。
 * 这样遍历子节点时不需要通过 unique_ptr 解引用。
 */
void LogicalOperator::generate_general_child()
{
  for (auto &child : children_) {
    general_children_.push_back(child.get());
    child->generate_general_child();  // ★ 递归
  }
}
