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
// Created by Meiyi
//

/**
 * ==========================================================================
 * ★★★【核心文件】parse_defs.h — SQL 语法树节点定义 ★★★
 * ==========================================================================
 *
 * ★ 这个文件定义了"一条 SQL 被解析后"的数据结构。
 *
 * 解析流程：SQL 字符串 → flex/bison 解析 → 填充这些结构体
 *
 * 核心类：ParsedSqlNode
 *   它是一个"万能容器"（tagged union），根据 flag 字段决定哪个子结构有效。
 *   类似 C 的 union，但用 C++ 的结构体实现。
 *
 *   ParsedSqlNode.flag = SCF_SELECT  → 用 .selection（SelectSqlNode）
 *   ParsedSqlNode.flag = SCF_INSERT  → 用 .insertion（InsertSqlNode）
 *   ParsedSqlNode.flag = SCF_CREATE_TABLE → 用 .create_table（CreateTableSqlNode）
 *
 * ★ 设计取舍：为什么用 tagged union 而不是继承体系？
 *   优点：简单直接，一个对象走遍所有阶段，不需要 dynamic_cast
 *   缺点：浪费内存（每个节点都包含所有类型的字段），不够类型安全
 *   对于 MiniOB 这种教学项目，tagged union 足够了
 *
 * 💡 提问：如果以后要加 50 种 SQL 语句，ParsedSqlNode 会有多大？
 *   真实数据库怎么解决这个问题？
 *   （提示：看看 OceanBase 的 ParseNode — 它用的是继承体系）
 * ==========================================================================
 */

#pragma once

#include "common/lang/string.h"
#include "common/lang/vector.h"
#include "common/lang/memory.h"
#include "common/value.h"
#include "common/lang/utility.h"

class Expression;

// ==========================================================================
// 基础元素
// ==========================================================================

/**
 * @brief RelAttrSqlNode — 表示"表.列"的引用
 *
 * 例：SELECT t1.id FROM t1
 *   relation_name = "t1"
 *   attribute_name = "id"
 *
 * 例：SELECT id FROM t1  （无表名前缀）
 *   relation_name = ""   （空）
 *   attribute_name = "id"
 *
 * 在 ResolveStage 中，空的 relation_name 会被补充为当前查询的表名。
 */
struct RelAttrSqlNode
{
  string relation_name;   ///< 表名（可以为空，表示不指定表）
  string attribute_name;  ///< 列名
};

/**
 * @brief 比较运算符枚举
 */
enum CompOp
{
  EQUAL_TO,     ///< "="
  LESS_EQUAL,   ///< "<="
  NOT_EQUAL,    ///< "<>"
  LESS_THAN,    ///< "<"
  GREAT_EQUAL,  ///< ">="
  GREAT_THAN,   ///< ">"
  NO_OP
};

/**
 * @brief ConditionSqlNode — WHERE 子句中的一个条件
 *
 * ★ left_is_attr / right_is_attr 是"标记位"模式（tagged value），
 *   表示 left/right 是列引用还是常量值。
 *
 * 例：WHERE id > 10
 *   left_is_attr=1, left_attr={relation="", attribute="id"}
 *   comp=GREAT_THAN
 *   right_is_attr=0, right_value=10
 *
 * 例：WHERE t1.id = t2.id  （表连接条件）
 *   left_is_attr=1, left_attr={relation="t1", attribute="id"}
 *   comp=EQUAL_TO
 *   right_is_attr=1, right_attr={relation="t2", attribute="id"}
 *
 * ★ 限制：只支持 "列 op 值"、"值 op 值"、"列 op 列"、"值 op 列" 四种模式
 *   不支持 "表达式 op 表达式"（如 WHERE age+1 > salary*2）
 */
struct ConditionSqlNode
{
  int left_is_attr;              ///< 1=左边是列引用，0=左边是常量值
  Value          left_value;     ///< 左边的常量值（left_is_attr=0 时有效）
  RelAttrSqlNode left_attr;      ///< 左边的列引用（left_is_attr=1 时有效）
  CompOp         comp;           ///< 比较运算符
  int            right_is_attr;  ///< 1=右边是列引用，0=右边是常量值
  RelAttrSqlNode right_attr;     ///< 右边的列引用
  Value          right_value;    ///< 右边的常量值
};

// ==========================================================================
// DML 语句的 AST 节点
// ==========================================================================

/**
 * @brief SelectSqlNode — SELECT 语句的 AST
 *
 * 例：SELECT id, name FROM t1, t2 WHERE t1.id_t2 = t2.id AND age > 20 GROUP BY city
 *   expressions = [UnboundFieldExpr(id), UnboundFieldExpr(name)]
 *   relations   = ["t1", "t2"]
 *   conditions  = [{t1.id_t2 = t2.id}, {age > 20}]  — AND 连接
 *   group_by    = [UnboundFieldExpr(city)]
 *
 * ★ 注意：这里的 Expression 是 "Unbound"（未绑定的），
 *   只在语法层面知道名字，还不知道对应的数据库对象。
 *   比如 "id" 到底对应哪张表的哪个列，要到 ResolveStage 才知道。
 */
struct SelectSqlNode
{
  vector<unique_ptr<Expression>> expressions;  ///< SELECT 后的列/表达式列表
  vector<string>                 relations;    ///< FROM 后的表名列表
  vector<ConditionSqlNode>       conditions;   ///< WHERE 条件（AND 串联）
  vector<unique_ptr<Expression>> group_by;     ///< GROUP BY 列列表
};

/**
 * @brief CalcSqlNode — CALC 计算表达式的 AST
 *
 * 例：CALC 1+2*3
 *   expressions = [ArithmeticExpr(ADD, ValueExpr(1), ArithmeticExpr(MUL, ValueExpr(2), ValueExpr(3)))]
 *
 * CALC 是 MiniOB 的特殊命令：直接在服务器端计算表达式值。
 * 用与 SELECT 相同的 Expression 体系，但没有 FROM/WHERE/GROUP BY。
 */
struct CalcSqlNode
{
  vector<unique_ptr<Expression>> expressions;  ///< 计算表达式列表
};

/**
 * @brief InsertSqlNode — INSERT 语句的 AST
 *
 * 例：INSERT INTO t1 VALUES (1, 'hello', 3.14)
 *   relation_name = "t1"
 *   values = [Value(1), Value("hello"), Value(3.14)]
 *
 * ★ 限制：不支持指定列名（INSERT INTO t1(id, name) VALUES(...)），
 *   必须按表定义顺序提供所有列的值。
 */
struct InsertSqlNode
{
  string        relation_name;  ///< 表名
  vector<Value> values;         ///< 值列表，按列定义顺序
};

/**
 * @brief DeleteSqlNode — DELETE 语句的 AST
 *
 * 例：DELETE FROM t1 WHERE id < 100
 *   relation_name = "t1"
 *   conditions = [{id < 100}]
 */
struct DeleteSqlNode
{
  string                   relation_name;
  vector<ConditionSqlNode> conditions;
};

/**
 * @brief UpdateSqlNode — UPDATE 语句的 AST
 *
 * ★ 限制：只支持 SET 单个列
 *
 * 例：UPDATE t1 SET name = 'Bob' WHERE id = 1
 *   relation_name = "t1"
 *   attribute_name = "name"
 *   value = Value("Bob")
 *   conditions = [{id = 1}]
 */
struct UpdateSqlNode
{
  string                   relation_name;   ///< 表名
  string                   attribute_name;  ///< 要更新的列名（仅一列）
  Value                    value;           ///< 新值
  vector<ConditionSqlNode> conditions;      ///< WHERE 条件
};

// ==========================================================================
// DDL 语句的 AST 节点
// ==========================================================================

/**
 * @brief AttrInfoSqlNode — 列定义
 *
 * 例：id int, name char(20)
 *   AttrInfoSqlNode{type=INTS, name="id", length=4}
 *   AttrInfoSqlNode{type=CHARS, name="name", length=20}
 */
struct AttrInfoSqlNode
{
  AttrType type;    ///< 数据类型（INTS/CHARS/FLOATS/VECTORS）
  string   name;    ///< 列名
  size_t   length;  ///< 长度（对 CHAR 有意义）
};

/**
 * @brief CreateTableSqlNode — CREATE TABLE 语句的 AST
 *
 * 例：CREATE TABLE t1 (id int, name char(20))
 *   relation_name = "t1"
 *   attr_infos = [{id, INTS}, {name, CHARS, 20}]
 */
struct CreateTableSqlNode
{
  string                  relation_name;
  vector<AttrInfoSqlNode> attr_infos;
  vector<string>          primary_keys;
  string                  storage_format;
  string                  storage_engine;
};

struct DropTableSqlNode
{
  string relation_name;
};

struct AnalyzeTableSqlNode
{
  string relation_name;
};

struct CreateIndexSqlNode
{
  string index_name;
  string relation_name;
  string attribute_name;  ///< 索引只支持单列
};

struct DropIndexSqlNode
{
  string index_name;
  string relation_name;
};

struct DescTableSqlNode
{
  string relation_name;
};

/**
 * @brief LoadDataSqlNode — LOAD DATA 语句的 AST
 *
 * 用途：从 CSV/文本文件批量导入数据
 * 例：LOAD DATA INFILE 'data.csv' INTO TABLE t1 FIELDS TERMINATED BY ','
 */
struct LoadDataSqlNode
{
  string relation_name;
  string file_name;
  string terminated = ",";
  string enclosed   = "\"";
};

struct SetVariableSqlNode
{
  string name;
  Value  value;
};

// ★ 前向声明：ParsedSqlNode 在 ExplainSqlNode 中使用，但定义在后面
class ParsedSqlNode;

/**
 * @brief ExplainSqlNode — EXPLAIN 语句的 AST
 *
 * ★ EXPLAIN 可以查看任何语句的执行计划。
 *   它内部包含一个完整的 ParsedSqlNode（被 EXPLAIN 的子语句）。
 *
 * 例：EXPLAIN SELECT * FROM t1
 *   sql_node = ParsedSqlNode{flag=SCF_SELECT, selection={...}}
 */
struct ExplainSqlNode
{
  unique_ptr<ParsedSqlNode> sql_node;  ///< 被 EXPLAIN 的子语句
};

struct CreateDatabaseSqlNode
{
  string db_name;
};

struct DropDatabaseSqlNode
{
  string db_name;
};

struct UseDatabaseSqlNode
{
  string db_name;
};

struct ErrorSqlNode
{
  string error_msg;
  int    line;
  int    column;
};

// ==========================================================================
// ★ SqlCommandFlag — SQL 语句类型枚举
// ==========================================================================
/**
 * 每个 flag 对应一种 SQL 语句类型。
 * yacc_sql.y 中每个语法规则创建一个 ParsedSqlNode 并设置对应的 flag。
 * 整个系统通过这个 flag 来判断"用户输入的是什么类型的 SQL"。
 */
enum SqlCommandFlag
{
  SCF_ERROR = 0,
  SCF_CALC,          ///< 计算表达式（CALC 1+2）
  SCF_SELECT,        ///< SELECT 查询
  SCF_INSERT,        ///< INSERT 插入
  SCF_UPDATE,        ///< UPDATE 更新
  SCF_DELETE,        ///< DELETE 删除
  SCF_CREATE_TABLE,  ///< CREATE TABLE 建表
  SCF_DROP_TABLE,    ///< DROP TABLE 删表
  SCF_ANALYZE_TABLE, ///< ANALYZE TABLE 分析表
  SCF_CREATE_INDEX,  ///< CREATE INDEX 创建索引
  SCF_DROP_INDEX,    ///< DROP INDEX 删除索引
  SCF_SYNC,          ///< SYNC 同步刷盘
  SCF_SHOW_TABLES,   ///< SHOW TABLES 显示所有表
  SCF_DESC_TABLE,    ///< DESC TABLE 查看表结构
  SCF_BEGIN,         ///< BEGIN 开始事务
  SCF_COMMIT,        ///< COMMIT 提交事务
  SCF_CLOG_SYNC,
  SCF_ROLLBACK,      ///< ROLLBACK 回滚事务
  SCF_LOAD_DATA,     ///< LOAD DATA 导入数据
  SCF_HELP,          ///< HELP 显示帮助
  SCF_EXIT,          ///< EXIT 退出
  SCF_EXPLAIN,       ///< EXPLAIN 显示执行计划
  SCF_SET_VARIABLE,      ///< SET 设置变量
  SCF_CREATE_DATABASE,   ///< CREATE DATABASE 创建数据库
  SCF_DROP_DATABASE,     ///< DROP DATABASE 删除数据库
  SCF_SHOW_DATABASES,    ///< SHOW DATABASES 显示所有数据库
  SCF_USE_DATABASE,      ///< USE DATABASE 切换数据库
};

// ==========================================================================
// ★★★ ParsedSqlNode — 核心：解析后的 SQL 节点（Tagged Union）★★★
// ==========================================================================
/**
 * ★ 这是 SQL 语法分析的最终产物。
 *
 * 解析流程中的角色：
 *   1. yacc_sql.y 语法规则创建 ParsedSqlNode，填充 flag + 对应子结构
 *   2. ParseStage 把它存入 SQLStageEvent
 *   3. ResolveStage 读取它，通过 Stmt::create_stmt() 转换成 Stmt
 *
 * ★ 内存布局：所有子结构都在同一个对象内（不是指针），
 *   这意味着每个 ParsedSqlNode 都很大（包含所有类型的字段）。
 *   对于教学项目无所谓，生产数据库会用继承体系。
 */
class ParsedSqlNode
{
public:
  enum SqlCommandFlag flag;  ///< ★ 关键字段：决定哪个子结构有效
  ErrorSqlNode        error;
  CalcSqlNode         calc;
  SelectSqlNode       selection;     ///< SELECT 语句的数据
  InsertSqlNode       insertion;     ///< INSERT 语句的数据
  DeleteSqlNode       deletion;      ///< DELETE 语句的数据
  UpdateSqlNode       update;        ///< UPDATE 语句的数据
  CreateTableSqlNode  create_table;  ///< CREATE TABLE 语句的数据
  DropTableSqlNode    drop_table;
  AnalyzeTableSqlNode analyze_table;
  CreateIndexSqlNode  create_index;
  DropIndexSqlNode    drop_index;
  DescTableSqlNode    desc_table;
  LoadDataSqlNode     load_data;
  ExplainSqlNode        explain;
  SetVariableSqlNode    set_variable;
  CreateDatabaseSqlNode create_database;
  DropDatabaseSqlNode   drop_database;
  UseDatabaseSqlNode    use_database;

public:
  ParsedSqlNode();
  explicit ParsedSqlNode(SqlCommandFlag flag);
};

/**
 * @brief ParsedSqlResult — 解析结果容器
 *
 * 存放一个或多个 ParsedSqlNode（虽然目前只处理一个）。
 * parse() 函数（由 flex+bison 生成）的输出。
 */
class ParsedSqlResult
{
public:
  void add_sql_node(unique_ptr<ParsedSqlNode> sql_node);
  vector<unique_ptr<ParsedSqlNode>> &sql_nodes() { return sql_nodes_; }
private:
  vector<unique_ptr<ParsedSqlNode>> sql_nodes_;
};
