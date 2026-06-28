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
 * ★★★【核心学习文件】stmt.cpp — Stmt 工厂方法实现 ★★★
 * ==========================================================================
 *
 * ★ 这个文件实现了一个"两级工厂"：
 *
 *   Stmt::create_stmt()  ← 一级工厂（根据 SqlCommandFlag 分发）
 *        │
 *        ├── SCF_SELECT  → SelectStmt::create()
 *        ├── SCF_INSERT  → InsertStmt::create()
 *        ├── SCF_DELETE  → DeleteStmt::create()
 *        ├── SCF_CREATE_TABLE → CreateTableStmt::create()
 *        └── SCF_COMMIT  → TrxEndStmt::create()
 *              ...
 *
 * ★ 为什么要两级工厂？
 *   一级工厂做"类型分发"（switch-case），二级做"语义绑定"（名字→对象）。
 *   这样 Stmt 基类不需要知道所有子类的创建细节，符合开闭原则：
 *   新增一个 Stmt 子类只需在 switch 中加一个 case，不需要修改基类。
 *
 * ★ ResolveStage 的调用链：
 *   ResolveStage::handle_request()
 *     → Stmt::create_stmt(db, sql_node, stmt)
 *       → SelectStmt::create(db, selection, stmt)
 *         → 从 Db 查找 Table 对象（字符串 → 指针）
 *         → 从 Table 查找 Field 对象（字符串 → 指针）
 *         → 构建 SelectStmt 对象
 *
 * 💡 提问：为什么 COMMIT 和 ROLLBACK 都对应 TrxEndStmt？
 *   （提示：它们的行为几乎一样（结束事务），只是结束方式不同。
 *          用一个 flag 区分子类型比创建两个 Stmt 类更经济）
 * ==========================================================================
 */

#include "sql/stmt/stmt.h"
#include "common/log/log.h"
#include "sql/stmt/analyze_table_stmt.h"
#include "sql/stmt/calc_stmt.h"
#include "sql/stmt/create_index_stmt.h"
#include "sql/stmt/create_table_stmt.h"
#include "sql/stmt/delete_stmt.h"
#include "sql/stmt/desc_table_stmt.h"
#include "sql/stmt/exit_stmt.h"
#include "sql/stmt/explain_stmt.h"
#include "sql/stmt/help_stmt.h"
#include "sql/stmt/insert_stmt.h"
#include "sql/stmt/load_data_stmt.h"
#include "sql/stmt/select_stmt.h"
#include "sql/stmt/set_variable_stmt.h"
#include "sql/stmt/show_tables_stmt.h"
#include "sql/stmt/show_databases_stmt.h"
#include "sql/stmt/create_database_stmt.h"
#include "sql/stmt/drop_database_stmt.h"
#include "sql/stmt/use_database_stmt.h"
#include "sql/stmt/trx_begin_stmt.h"
#include "sql/stmt/trx_end_stmt.h"

/**
 * ★ stmt_type_ddl — 判断是否是 DDL 语句
 *
 * DDL 语句的特征：
 *   - 修改元数据（表定义、索引定义等）
 *   - 执行后需要 sync 持久化到磁盘
 *   - 不走物理算子路径（不需要 TableScan/Predicate/Project）
 *   - 直接通过 CommandExecutor 执行
 *
 * 💡 提问：为什么 DDL 需要 sync 而 DML 不需要？
 *   （提示：DDL 修改的是元数据（表结构），丢失元数据会导致数据库无法启动。
 *          DML 修改的是用户数据，可以容忍少量丢失（WAL 恢复）。
 *          对于 MiniOB 的教学实现，sync 是简化版的持久化保证）
 */
bool stmt_type_ddl(StmtType type)
{
  switch (type) {
    case StmtType::CREATE_TABLE:
    case StmtType::DROP_TABLE:
    case StmtType::DROP_INDEX:
    case StmtType::CREATE_INDEX:
    case StmtType::CREATE_DATABASE:
    case StmtType::DROP_DATABASE: {
      return true;
    }
    default: {
      return false;
    }
  }
}

/**
 * ★★★ Stmt::create_stmt — 一级工厂：类型分发 ★★★
 *
 * 输入：ParsedSqlNode（语法树 tagged union）
 * 输出：Stmt*（语义分析后的语句对象）
 *
 * 处理流程：
 *   1. 根据 sql_node.flag 找到对应的子结构（如 SCF_SELECT → sql_node.selection）
 *   2. 调用对应 Stmt 子类的 create() 静态方法
 *   3. 子类的 create() 会：
 *      - 通过 db 查找 Table 对象（字符串 → 指针）
 *      - 验证列名是否存在
 *      - 创建表达式对象
 *      - 构建完整的 Stmt 对象
 *
 * ★ 为什么 flag 用 switch-case 而不是虚函数或函数表？
 *   ParsedSqlNode 是 tagged union 结构，不是多态类，没有虚函数。
 *   switch-case 是 tagged union 的标准分发方式。
 *   如果想要更优雅（但更复杂），可以用函数指针表（flag → create_fn）。
 *
 * ★ 为什么一些 Stmt 只是简单的 return XXXStmt::create(stmt) 而不传 db？
 *   Help/Exit/Begin 这些语句不需要访问数据库元数据，
 *   不需要查找 Table/Field，所以不需要传 Db*。
 *
 * 💡 提问：如果新增一种 SQL 语句（比如 TRUNCATE TABLE），
 *   需要修改哪些文件？
 *   （提示：parse_defs.h 加 SqlCommandFlag + AST 节点，
 *         yacc_sql.y 加语法规则，
 *         stmt.h 加 StmtType 枚举，
 *         stmt.cpp 加 case 分支，
 *         新建 truncate_stmt.h/cpp）
 */
RC Stmt::create_stmt(Db *db, ParsedSqlNode &sql_node, Stmt *&stmt)
{
  stmt = nullptr;

  switch (sql_node.flag) {
    // ★ DML 语句 — 需要 db 参数来查找表
    case SCF_INSERT: {
      return InsertStmt::create(db, sql_node.insertion, stmt);
    }
    case SCF_DELETE: {
      return DeleteStmt::create(db, sql_node.deletion, stmt);
    }
    case SCF_SELECT: {
      return SelectStmt::create(db, sql_node.selection, stmt);
    }

    case SCF_EXPLAIN: {
      return ExplainStmt::create(db, sql_node.explain, stmt);
    }

    // ★ DDL 语句 — 需要 db 参数来查找/修改元数据
    case SCF_CREATE_INDEX: {
      return RC::UNIMPLEMENTED;
    }

    case SCF_CREATE_TABLE: {
      return CreateTableStmt::create(db, sql_node.create_table, stmt);
    }

    case SCF_DESC_TABLE: { stmt = new DescTableStmt(""); return RC::SUCCESS; }
    case SCF_ANALYZE_TABLE: { stmt = new AnalyzeTableStmt(""); return RC::SUCCESS; }

    // ★ 无需 db 参数的语句 — 纯控制/显示类命令
    case SCF_HELP: {
      return HelpStmt::create(stmt);
    }

    case SCF_SHOW_TABLES: {
      return ShowTablesStmt::create(db, stmt);
    }

    case SCF_SHOW_DATABASES: {
      return ShowDatabasesStmt::create(db, stmt);
    }

    case SCF_CREATE_DATABASE: {
      return CreateDatabaseStmt::create(db, sql_node.create_database, stmt);
    }

    case SCF_DROP_DATABASE: {
      return DropDatabaseStmt::create(db, sql_node.drop_database, stmt);
    }

    case SCF_USE_DATABASE: {
      return UseDatabaseStmt::create(db, sql_node.use_database, stmt);
    }

    case SCF_BEGIN: {
      return TrxBeginStmt::create(stmt);
    }

    // ★ SCF_COMMIT 和 SCF_ROLLBACK 都映射到 TrxEndStmt
    // flag（COMMIT vs ROLLBACK）作为参数传入，在 Stmt 内部区分行为
    case SCF_COMMIT:
    case SCF_ROLLBACK: {
      return TrxEndStmt::create(sql_node.flag, stmt);
    }

    case SCF_EXIT: {
      return ExitStmt::create(stmt);
    }

    case SCF_SET_VARIABLE: {
      return SetVariableStmt::create(sql_node.set_variable, stmt);
    }

    case SCF_LOAD_DATA: {
      return LoadDataStmt::create(db, sql_node.load_data, stmt);
    }

    case SCF_CALC: {
      return CalcStmt::create(sql_node.calc, stmt);
    }

    default: {
      // ★ 不需要创建 Stmt 的命令（SCF_SYNC、SCF_CLOG_SYNC 等）
      // 这些命令在更早的阶段（ParseStage）已经被处理或忽略
      LOG_INFO("Command::type %d doesn't need to create statement.", sql_node.flag);
    } break;
  }
  return RC::UNIMPLEMENTED;
}
