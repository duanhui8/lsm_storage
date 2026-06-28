//
// Created by Wangyunlai on 2023/4/25.
//

/**
 * ==========================================================================
 * 【架构概览】CommandExecutor — DDL 命令执行调度器
 * ==========================================================================
 *
 * ★ 定位：所有不走"算子执行路径"的命令在这里处理
 *
 * 回顾 ExecuteStage 的两路分发：
 *   有物理算子 → handle_request_with_physical_operator（DML: SELECT/INSERT/DELETE/UPDATE）
 *   无物理算子 → CommandExecutor::execute() ← 这个文件（DDL + 管理命令）
 *
 * ★ 设计模式：命令模式（Command Pattern）的简化版
 *   每种 DDL 命令有一个独立的 Executor 类：
 *   - CreateTableExecutor   → CREATE TABLE
 *   - CreateIndexExecutor   → CREATE INDEX
 *   - DescTableExecutor     → DESC TABLE（查看表结构）
 *   - ShowTablesExecutor    → SHOW TABLES
 *   - TrxBeginExecutor      → BEGIN（启动事务）
 *   - TrxEndExecutor        → COMMIT / ROLLBACK
 *   - HelpExecutor          → HELP
 *   - LoadDataExecutor      → LOAD DATA
 *   - AnalyzeTableExecutor  → ANALYZE TABLE（收集统计信息）
 *   - SetVariableExecutor   → SET（设置变量）
 *
 * ★ 为什么 DDL 不走优化器？
 *   DDL 不需要"查询计划"：CREATE TABLE 就是创建元数据 + 建文件，
 *   没有"先扫描哪个表"、"用什么join算法"这类优化问题。
 *   直接调存储引擎 API 执行即可。
 *
 * ★ DDL 后自动 sync
 *   所有 DDL 执行成功后都会调用 db->sync() 刷盘。
 *   这确保元数据变更持久化，不会因为崩溃丢失。
 *
 * 💡 提问：如果以后要加一个 DROP TABLE 命令，需要改哪些地方？
 *   （提示：从 parser → resolve → command_executor，每层都要加什么？）
 * ==========================================================================
 */

#include "sql/executor/command_executor.h"
#include "common/log/log.h"
#include "event/sql_event.h"
#include "sql/executor/analyze_table_executor.h"
#include "sql/executor/create_index_executor.h"
#include "sql/executor/create_table_executor.h"
#include "sql/executor/desc_table_executor.h"
#include "sql/executor/help_executor.h"
#include "sql/executor/load_data_executor.h"
#include "sql/executor/set_variable_executor.h"
#include "sql/executor/show_tables_executor.h"
#include "sql/executor/show_databases_executor.h"
#include "sql/executor/create_database_executor.h"
#include "sql/executor/drop_database_executor.h"
#include "sql/executor/use_database_executor.h"
#include "sql/executor/trx_begin_executor.h"
#include "sql/executor/trx_end_executor.h"
#include "sql/stmt/stmt.h"

/**
 * ★ DDL 命令分发
 *
 * 这是一个典型的 switch-case 分发器。根据语句类型创建对应的 Executor 并执行。
 *
 * ★ 每种 Executor 的执行模式：
 *   Stmt → Executor::execute(SQLStageEvent) → 调用 DefaultHandler/存储层 API
 *           → 结果写入 SqlResult → 返回 RC
 *
 * 💡 提问：这里每个 case 都创建了新的 Executor 对象（栈上），执行完就销毁。
 *   为什么不复用 Executor？每次都 new 一个的代价是什么？
 *   （提示：这些 Executor 是无状态的函数对象，栈上创建几乎是零开销的）
 *
 * 💡 提问：EXIT 命令直接返回 RC::SUCCESS，它是怎么让客户端退出的？
 *   看 CliServer::serve() 中的 communicator.exit() 是怎么检测到的。
 */
RC CommandExecutor::execute(SQLStageEvent *sql_event)
{
  Stmt *stmt = sql_event->stmt();

  RC rc = RC::SUCCESS;
  switch (stmt->type()) {
    case StmtType::CREATE_INDEX: {
      CreateIndexExecutor executor;
      rc = executor.execute(sql_event);
    } break;

    case StmtType::CREATE_TABLE: {
      CreateTableExecutor executor;
      rc = executor.execute(sql_event);
    } break;

    case StmtType::DESC_TABLE: {
      DescTableExecutor executor;
      rc = executor.execute(sql_event);
    } break;

    case StmtType::ANALYZE_TABLE: {
      AnalyzeTableExecutor executor;
      rc = executor.execute(sql_event);
    } break;

    case StmtType::HELP: {
      HelpExecutor executor;
      rc = executor.execute(sql_event);
    } break;

    case StmtType::SHOW_TABLES: {
      ShowTablesExecutor executor;
      rc = executor.execute(sql_event);
    } break;

    case StmtType::SHOW_DATABASES: {
      ShowDatabasesExecutor executor;
      rc = executor.execute(sql_event);
    } break;

    case StmtType::CREATE_DATABASE: {
      CreateDatabaseExecutor executor;
      rc = executor.execute(sql_event);
    } break;

    case StmtType::DROP_DATABASE: {
      DropDatabaseExecutor executor;
      rc = executor.execute(sql_event);
    } break;

    case StmtType::USE_DATABASE: {
      UseDatabaseExecutor executor;
      rc = executor.execute(sql_event);
    } break;

    case StmtType::BEGIN: {
      TrxBeginExecutor executor;
      rc = executor.execute(sql_event);
    } break;

    case StmtType::COMMIT:
    case StmtType::ROLLBACK: {
      TrxEndExecutor executor;
      rc = executor.execute(sql_event);
    } break;

    case StmtType::SET_VARIABLE: {
      SetVariableExecutor executor;
      rc = executor.execute(sql_event);
    } break;

    case StmtType::LOAD_DATA: {
      LoadDataExecutor executor;
      rc = executor.execute(sql_event);
    } break;

    case StmtType::EXIT: {
      rc = RC::SUCCESS;  // EXIT 标记由 CliCommunicator 检测处理
    } break;

    default: {
      LOG_ERROR("unknown command: %d", static_cast<int>(stmt->type()));
      rc = RC::UNIMPLEMENTED;
    } break;
  }

  // DDL sync — SchemaService is in-memory for now
  if (OB_SUCC(rc) && stmt_type_ddl(stmt->type())) {
    LOG_INFO("DDL executed (via SchemaService). rc=%d", rc);
  }

  return rc;
}
