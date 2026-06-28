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
// Created by Wangyunlai on 2022/5/22.
//

/**
 * ==========================================================================
 * ★★★【核心学习文件】stmt.h — Stmt 抽象基类 + X-Macro 枚举 ★★★
 * ==========================================================================
 *
 * ★ 定位：Stmt 是"语义分析后的语句对象"。
 *   它区别于 ParsedSqlNode（语法树），Stmt 已经完成了"名字 → 对象"的绑定。
 *
 *   ParsedSqlNode:  relation_name = "t1"         ← 还是字符串
 *   Stmt:           table_ = Table* 指针           ← 已绑定到实际 Table 对象
 *
 * ★ 转换发生在 ResolveStage::handle_request() 中：
 *   ParsedSqlNode → Stmt::create_stmt() → 具体 Stmt 子类
 *
 * ★ 设计模式：
 *   1. X-Macro（DEFINE_ENUM）实现枚举 → 字符串的自动映射
 *   2. 工厂方法 create_stmt() 根据 SqlCommandFlag 创建对应 Stmt 子类
 *   3. 每个 Stmt 子类有自己的 static create() 方法（具体工厂）
 *
 * 💡 提问：为什么 ParsedSqlNode 用 tagged union，而 Stmt 用继承体系？
 *   （提示：Stmt 需要多态行为 — 不同语句有不同的优化和执行逻辑，
 *          ParsedSqlNode 只是"数据容器"，不需要多态）
 *
 * 💡 提问：为什么每个 Stmt 子类的 create() 是 static 方法而不是普通构造函数？
 *   （提示：create 过程可能失败（表不存在等），构造函数没法返回错误码）
 * ==========================================================================
 */

#pragma once

#include "common/sys/rc.h"
#include "sql/parser/parse_defs.h"

class Db;

/**
 * ==========================================================================
 * ★ StmtType 枚举 — 使用 X-Macro 模式
 * ==========================================================================
 *
 * ★ X-Macro 是什么？
 *   把枚举值列表定义成宏 DEFINE_ENUM()，然后在不同地方"展开"它：
 *   - 第一次展开：生成 enum class 的成员
 *   - 第二次展开：生成 switch-case 的名字字符串
 *
 * 好处：新增语句类型只需改一处（DEFINE_ENUM），
 *       枚举定义和名字字符串自动同步，不会漏改。
 *
 * 对比普通写法：
 *   enum class StmtType { SELECT, INSERT, DELETE };  // 手动维护
 *   const char* name(StmtType t) {                   // 手动维护，容易遗漏
 *     switch(t) { case SELECT: return "SELECT"; ... }
 *   }
 *
 * ★ MiniOB 大量使用 X-Macro：StmtType、SqlCommandFlag、AttrType 都用这个模式。
 */
#define DEFINE_ENUM()             \
  DEFINE_ENUM_ITEM(CALC)          \
  DEFINE_ENUM_ITEM(SELECT)        \
  DEFINE_ENUM_ITEM(INSERT)        \
  DEFINE_ENUM_ITEM(UPDATE)        \
  DEFINE_ENUM_ITEM(DELETE)        \
  DEFINE_ENUM_ITEM(CREATE_TABLE)  \
  DEFINE_ENUM_ITEM(DROP_TABLE)    \
  DEFINE_ENUM_ITEM(ANALYZE_TABLE) \
  DEFINE_ENUM_ITEM(CREATE_INDEX)  \
  DEFINE_ENUM_ITEM(DROP_INDEX)    \
  DEFINE_ENUM_ITEM(SYNC)          \
  DEFINE_ENUM_ITEM(SHOW_TABLES)   \
  DEFINE_ENUM_ITEM(DESC_TABLE)    \
  DEFINE_ENUM_ITEM(BEGIN)         \
  DEFINE_ENUM_ITEM(COMMIT)        \
  DEFINE_ENUM_ITEM(ROLLBACK)      \
  DEFINE_ENUM_ITEM(LOAD_DATA)     \
  DEFINE_ENUM_ITEM(HELP)          \
  DEFINE_ENUM_ITEM(EXIT)          \
  DEFINE_ENUM_ITEM(EXPLAIN)       \
  DEFINE_ENUM_ITEM(PREDICATE)     \
  DEFINE_ENUM_ITEM(SET_VARIABLE) \
  DEFINE_ENUM_ITEM(CREATE_DATABASE) \
  DEFINE_ENUM_ITEM(DROP_DATABASE) \
  DEFINE_ENUM_ITEM(SHOW_DATABASES) \
  DEFINE_ENUM_ITEM(USE_DATABASE)

// ★ 第一次展开：生成枚举成员
enum class StmtType
{
#define DEFINE_ENUM_ITEM(name) name,
  DEFINE_ENUM()
#undef DEFINE_ENUM_ITEM
};

// ★ 第二次展开：生成枚举 → 字符串的转换函数
inline const char *stmt_type_name(StmtType type)
{
  switch (type) {
#define DEFINE_ENUM_ITEM(name) \
  case StmtType::name: return #name;
    DEFINE_ENUM()
#undef DEFINE_ENUM_ITEM
    default: return "unkown";
  }
}

/**
 * @brief 判断一个 StmtType 是否是 DDL 语句
 *
 * DDL（Data Definition Language）语句需要特殊处理：
 *   - 执行后需要 sync（刷盘持久化）
 *   - 不走优化器（不需要生成物理执行计划）
 *   - 走 CommandExecutor 路径（不是物理算子路径）
 */
bool stmt_type_ddl(StmtType type);

/**
 * ==========================================================================
 * ★ Stmt — 所有语句对象的抽象基类
 * ==========================================================================
 *
 * ★ 继承层次：
 *   Stmt (抽象基类)
 *   ├── SelectStmt       — SELECT 语句（最复杂）
 *   ├── InsertStmt       — INSERT 语句
 *   ├── DeleteStmt       — DELETE 语句
 *   ├── UpdateStmt       — UPDATE 语句（未实现）
 *   ├── CreateTableStmt  — CREATE TABLE 语句
 *   ├── CreateIndexStmt  — CREATE INDEX 语句
 *   ├── DropTableStmt    — DROP TABLE 语句
 *   ├── DropIndexStmt    — DROP INDEX 语句
 *   ├── DescTableStmt    — DESC TABLE 语句
 *   ├── ShowTablesStmt   — SHOW TABLES 语句
 *   ├── TrxBeginStmt     — BEGIN 语句
 *   ├── TrxEndStmt       — COMMIT / ROLLBACK 语句
 *   ├── ExplainStmt      — EXPLAIN 语句
 *   ├── LoadDataStmt     — LOAD DATA 语句
 *   ├── CalcStmt         — CALC 计算表达式
 *   ├── HelpStmt         — HELP 语句
 *   ├── ExitStmt         — EXIT 语句
 *   └── SetVariableStmt  — SET 变量语句
 *
 * ★ 每个子类需要实现：
 *   - type() — 返回 StmtType 枚举
 *   - static create() — 从 ParsedSqlNode 子结构构建 Stmt
 *
 * 💡 提问：为什么 Stmt 的析构函数是 virtual 的？
 *   （提示：如果通过基类指针删除子类对象，非虚析构函数只会调用基类的析构，
 *          子类成员的内存就泄漏了。虽然这个项目用 unique_ptr<Stmt>，
 *          编译器能推导具体类型，但加上 virtual 是最佳实践）
 */
class Stmt
{
public:
  Stmt()          = default;
  virtual ~Stmt() = default;

  virtual StmtType type() const = 0;

public:
  /**
   * ★★★ 核心工厂方法 — 从 ParsedSqlNode 创建对应的 Stmt 子类 ★★★
   *
   * 这是 ResolveStage 调用的入口。
   * 根据 sql_node.flag 分发到各子类的 create() 静态工厂方法。
   *
   * 参数：
   *   db       — 数据库对象，用于查找表/索引等数据库对象
   *   sql_node — 解析后的语法树节点（tagged union）
   *   stmt     — 输出参数，创建成功后的 Stmt 指针
   *
   * 返回值：
   *   RC::SUCCESS — 创建成功
   *   RC::SCHEMA_TABLE_NOT_EXIST — 表不存在
   *   RC::SCHEMA_FIELD_NOT_EXIST — 列不存在
   *   RC::UNIMPLEMENTED — 该语句类型不需要创建 Stmt
   *
   * 💡 提问：为什么 parse_defs.h 中的 SqlCommandFlag 和 stmt.h 中的 StmtType
   *   各有一套枚举？为什么不用同一套？
   *   （提示：SqlCommandFlag 对应的是"语法层面"的语句类型，
   *          StmtType 对应的是"语义层面"的语句类型。
   *          比如 SCF_COMMIT 和 SCF_ROLLBACK 是两个不同的语法，
   *          但都对应同一个 TrxEndStmt — 语法和语义不是一对一的关系）
   */
  static RC create_stmt(Db *db, ParsedSqlNode &sql_node, Stmt *&stmt);

private:
};
