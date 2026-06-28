/**
 * ==========================================================================
 * 【核心学习文件】三大算子 — SELECT 执行树的骨架
 * ==========================================================================
 *
 * 这三个文件 Together 是理解 SELECT 执行机制的关键。
 * 它们按 "TableScan → Predicate → Project" 的顺序组成算子树：
 *
 *   Project(投影) → Predicate(过滤) → TableScan(扫描)
 *
 * ★ 这是火山模型最直观的体现：每个算子的 next() 调用子算子的 next()
 *
 * 建议按以下顺序阅读：
 *   1. TableScan（叶子节点，从存储引擎拿数据）
 *   2. Predicate（中间节点，按 WHERE 条件过滤）
 *   3. Project（根节点，按 SELECT 列裁剪输出）
 *
 * 关键文件位置：
 *   sql/operator/table_scan_physical_operator.h/.cpp
 *   sql/operator/predicate_physical_operator.h/.cpp
 *   sql/operator/project_physical_operator.h/.cpp
 *
 * 💡 综合提问：如果 SELECT * FROM t1（没有 WHERE 条件），
 *    算子树只有 TableScan → Project 还是也包括 Predicate？
 *    （提示：看 logical_plan_generator.cpp 的 create_plan(SelectStmt)，
 *           predicate_oper 为 nullptr 时怎么处理？）
 * ==========================================================================
 */

// ==========================================================================
// ★ TableScanPhysicalOperator — 叶子节点，从存储引擎读取行
// ==========================================================================

class TableScanPhysicalOperator : public PhysicalOperator
{
  /**
   * ★ open() — 打开表扫描
   *
   * 调用 table_->get_record_scanner() 获取 RecordScanner 迭代器。
   * RecordScanner 是存储引擎层的抽象，对 Heap 引擎返回 HeapRecordScanner，
   * 对 LSM 引擎返回 LsmRecordScanner（但目前 LSM 的 get_record_scanner 返回 UNIMPLEMENTED）。
   *
   * ★ 关键：open 只做初始化（获取扫描器、设置 schema），不读数据。
   *    数据在 next() 时按需读取（懒加载）。
   *
   * 💡 提问：为什么 open 不直接读第一行？
   *   （提示：如果查询是 "SELECT 1"（没有 FROM 子句），
   *          根本不需要 open TableScan，提前读第一行就浪费了）
   */
  RC open(Trx *trx);

  /**
   * ★ next() — 火山模型的核心
   *
   * 逻辑：
   *   while (record_scanner_->next(record)) {  // 从存储引擎拿下一行
   *     filter(tuple, result);                 // 应用谓词过滤
   *     if (result) break;                     // 满足条件 → 返回这行
   *   }
   *
   * ★ 谓词下推（Predicate Pushdown）：
   *   注意这里有 filter() 调用！这不是在 Predicate 算子中过滤的，
   *   而是优化器把一部分谓词"下推"到了 TableScan 层，在存储层就过滤。
   *   这减少了不必要的数据传输。
   *
   * 但诡异的是：如果 already 有 Predicate 算子，
   * 为什么还要在 TableScan 里再 filter 一遍？
   * → 这是两阶段过滤：TableScan 的 filter 是粗过滤（索引级别的条件），
   *   Predicate 算子做精过滤（表达式级别的条件）。
   *
   * 💡 提问：如果没有下推任何谓词（predicates_ 为空），filter 直接返回 true。
   *    这意味着什么？每行都直接通过吗？
   */
  RC next();

  /**
   * ★ 算子等价性判断
   * 两个 TableScan 算子是否"相同"取决于操作的表是否相同，
   * 不关心具体的 RecordScanner 实例或当前扫描位置。
   * 这用于计划缓存：如果两个查询扫描同一个表，可以复用执行计划。
   */
  bool operator==(const OperatorNode &other) const;
};


// ==========================================================================
// ★ PredicatePhysicalOperator — 中间节点，按 WHERE 条件过滤行
// ==========================================================================

class PredicatePhysicalOperator : public PhysicalOperator
{
  /**
   * ★ 构造函数
   *
   * expression_ 是一个表达式树，会被求值为布尔值。
   * 例如 WHERE id > 10 AND name = 'Alice' 对应的 expression 是：
   *   ConjunctionExpr(AND)
   *     ├── ComparisonExpr(GT, FieldExpr(id), ValueExpr(10))
   *     └── ComparisonExpr(EQ, FieldExpr(name), ValueExpr('Alice'))
   *
   * ASSERT 确保表达式求值结果必须是布尔类型。
   */
  PredicatePhysicalOperator(unique_ptr<Expression> expr);

  /**
   * ★ open() — 只有一个子节点
   *
   * Predicate 算子只有一个子节点（单输入算子）。
   * children_[0] 通常是 TableScan 或另一个 Predicate（多条件时可能合并）。
   *
   * ASSERT(children_.size() == 1) 确保：如果子节点数不是 1，说明优化器出 bug 了。
   */
  RC open(Trx *trx);

  /**
   * ★ next() — 逐行过滤
   *
   * 逻辑：
   *   while (child->next() == SUCCESS) {
   *     tuple = child->current_tuple();
   *     expression_->get_value(tuple, value);  // 计算表达式值
   *     if (value.get_boolean()) return SUCCESS;  // 满足条件 → 返回
   *     // 不满足 → 继续循环，跳过这一行
   *   }
   *   return RC::RECORD_EOF;  // 子节点数据耗尽
   *
   * ★ 关键特性：跳过不满足条件的行
   *   Predicate 的 next() 会一直循环直到找到一行满足条件的，
   *   或者子节点数据耗尽。这意味着 Predicate 的结果行数 ≤ 输入行数。
   *
   * 💡 提问：如果 WHERE 条件是 "id > 0" 而表里所有 id 都 > 0，
   *    这个 while 循环每次只执行一次，这不是浪费吗？
   *    有没有办法优化掉这种"永远为真"的条件？
   *   （提示：想想优化器的常量折叠和条件简化）
   */
  RC next();

  /**
   * ★ current_tuple() — 透传给子节点
   *
   * Predicate 不修改数据，只做过滤判断。
   * 所以当前的 Tuple 就是子节点的 current_tuple。
   */
  Tuple *current_tuple();
};


// ==========================================================================
// ★ ProjectPhysicalOperator — 根节点，按 SELECT 列裁剪输出
// ==========================================================================

class ProjectPhysicalOperator : public PhysicalOperator
{
  /**
   * ★ 构造函数
   *
   * expressions_ 是 SELECT 后面指定的列表达式列表。
   * 例如 SELECT id, name, age+1 FROM t1：
   *   expressions_ = [FieldExpr(id), FieldExpr(name), ArithmeticExpr(ADD, FieldExpr(age), ValueExpr(1))]
   *
   * 每个表达式会在 current_tuple() 中被求值，结果组成输出行。
   */
  ProjectPhysicalOperator(vector<unique_ptr<Expression>> &&expressions);

  /**
   * ★ next() — 最简单：直接透传给子节点
   *
   * Project 算子不改变行数，只改变行内容。
   * next() 直接调用 children_[0]->next()，不过滤也不增加行。
   *
   * 如果子节点是 Predicate，则 Predicate 已经过滤过了。
   * 如果子节点是 TableScan（没有 WHERE 子句时），直接透传。
   *
   * 如果 children_ 为空（如 SELECT 1+1，没有 FROM 子句），
   * 直接返回 RC::RECORD_EOF — 没有数据源。
   *
   * 💡 提问：为什么 Project 的 next() 这么简单，而改写行的逻辑在 current_tuple()？
   *    （提示：next 推进迭代器位置，current_tuple 获取当前位置的数据。
   *           这两个操作可以分别调用，各自有各自的含义。）
   */
  RC next();

  /**
   * ★ current_tuple() — 表达式求值
   *
   * 这是 Project 算子的核心：把原始行转换成只包含 SELECT 列的投影行。
   * 通过 ExpressionTuple 对 expressions_ 中的每个表达式求值。
   *
   * 例：原始行 {id=1, name='Alice', age=25, city='NYC'}
   *     SELECT id, name — 投影后 → {id=1, name='Alice'}
   *     SELECT id, age+1 — 投影后 → {id=1, age+1=26}
   *
   * ExpressionTuple::set_tuple() 把子节点的 Tuple 作为"输入列"，
   * 然后对每个 expression_ 求值，结果组成"输出列"。
   */
  Tuple *current_tuple();

  /**
   * ★ tuple_schema() — 定义输出列的 schema
   *
   * 每个表达式对应一列，列名从表达式的 name() 获取。
   * 这决定了写入客户端时的列头（比如 "id | name | age"）。
   */
  RC tuple_schema(TupleSchema &schema) const;
};
