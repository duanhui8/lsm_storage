/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

/**
 * ==========================================================================
 * ★★★ PredicateToJoinRule — 谓词转 JOIN 条件 ★★★
 * ==========================================================================
 *
 * ★ 说明：
 *   这是一个预留的优化规则，用于将 WHERE 子句中的等值条件
 *   转换为 JOIN ON 条件。
 *
 *   比如 SELECT * FROM t1, t2 WHERE t1.a = t2.b
 *   可以将 "t1.a = t2.b" 从 Filter 层面提升为 JOIN 条件，
 *   这样优化器就知道可以用 HashJoin 或 SortMergeJoin 而非
 *   NestedLoopJoin（笛卡尔积 + 过滤）。
 *
 * ★ 当前状态：空实现（接口已声明，规则逻辑待实现）。
 *   属于 MiniOB 的 TODO 项——将来的优化能力。
 * ==========================================================================
 */

#include "sql/optimizer/predicate_to_join_rule.h"
