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
 * ★★★ OptimizerUtils — 优化器工具函数 ★★★
 * ==========================================================================
 *
 * ★ 作用：将物理执行计划以 ASCII 树的形式打印出来
 *
 * 示例输出：
 *   OPERATOR(NAME)
 *   └─PROJECT
 *     └─HASH_JOIN(t1.a=t2.b)
 *       ├─TABLE_SCAN(t1)
 *       └─TABLE_SCAN(t2)
 *
 * ★ 实现细节：
 *   - 用 ends 向量追踪每层是否是"最后一个子节点"，
 *     决定画 "├─" 还是 "└─"，以及后续行是画 "│ " 还是 "  "
 *   - name() 返回算子名称（如 "TABLE_SCAN"）
 *   - param() 返回算子参数（如 "t1" 或者 "t1.a=t2.b"）
 *   - 递归遍历 children，类似树的 DFS
 *
 * ★ 使用场景：
 *   在 OptimizeStage 中，CBO 路径执行后会调用此函数打印物理计划，
 *   方便调试和理解优化器的决策。
 * ==========================================================================
 */

#include "sql/optimizer/optimizer_utils.h"

string OptimizerUtils::dump_physical_plan(const unique_ptr<PhysicalOperator>& children)
{
  // ★ 递归 lambda：用 ASCII 艺术画出计划树
  std::function<void(ostream &, PhysicalOperator *, int, bool, vector<uint8_t> &)> to_string = [&](
    ostream &os, PhysicalOperator *oper, int level, bool last_child, vector<uint8_t> &ends)
  {
    // ★ 绘制树结构线
    for (int i = 0; i < level - 1; i++) {
      if (ends[i]) {
        os << "  ";     // ★ 该层级已无更多兄弟 → 空白
      } else {
        os << "│ ";     // ★ 该层级还有兄弟 → 竖线
      }
    }
    if (level > 0) {
      if (last_child) {
        os << "└─";           // ★ 最后一个子节点 → 拐角
        ends[level - 1] = 1;  // ★ 标记该层已结束
      } else {
        os << "├─";           // ★ 还有兄弟 → 分支
      }
    }

    // ★ 输出算子名称和参数
    os << oper->name();
    string param = oper->param();
    if (!param.empty()) {
      os << "(" << param << ")";
    }
    os << '\n';

    // ★ 预留下一层的 ends 标记
    if (static_cast<int>(ends.size()) < level + 2) {
      ends.resize(level + 2);
    }
    ends[level + 1] = 0;

    // ★ 递归输出子节点
    vector<unique_ptr<PhysicalOperator>> &children = oper->children();
    const auto size = static_cast<int>(children.size());
    for (auto i = 0; i < size - 1; i++) {
      to_string(os, children[i].get(), level + 1, false /*last_child*/, ends);
    }
    if (size > 0) {
      to_string(os, children[size - 1].get(), level + 1, true /*last_child*/, ends);
    }
  };

  stringstream ss;
  ss << "OPERATOR(NAME)\n";

  int               level = 0;
  vector<uint8_t> ends;
  ends.push_back(true);
  to_string(ss, children.get(), level, true /*last_child*/, ends);

  return ss.str();
}
