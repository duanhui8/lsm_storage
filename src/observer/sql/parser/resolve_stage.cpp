//
// Created by Longda on 2021/4/13.
//

/**
 * ==========================================================================
 * 【架构概览】ResolveStage — 语义解析阶段
 * ==========================================================================
 *
 * ★ 定位：ParseStage 产出的是"语法树"（知道有哪些词、怎么组合的），
 *    ResolveStage 产出的是"语句对象"（知道这些词对应什么数据库对象）。
 *
 * 关键区别：
 *   ParseStage：SELECT * FROM t1 WHERE id > 10
 *     → ParsedSqlNode { type=SCF_SELECT, 表名="t1", 列名="*", 条件="id > 10" }
 *     → 知道"有个东西叫 t1"，但不知道 t1 是否存在
 *
 *   ResolveStage：ParsedSqlNode → SelectStmt
 *     → SelectStmt { table=Table对象指针, 列=FieldMeta指针, 条件=FilterStmt }
 *     → ★ 真正去 Db::find_table("t1") 查表是否存在，列是否匹配
 *
 * 这就是为什么 SELECT * FROM not_exist_table 在解析阶段不会报错，
 * 要到 resolve 阶段才报 "表不存在"。
 *
 * ★ 核心调用：Stmt::create_stmt(db, *sql_node, stmt)
 *   → 这是个工厂方法，根据 sql_node->flag 创建不同类型的 Stmt：
 *     SCF_SELECT → SelectStmt
 *     SCF_INSERT → InsertStmt
 *     SCF_CREATE_TABLE → CreateTableStmt
 *     ...
 *
 * 💡 提问：为什么要把"语法检查"和"语义检查"分开？
 *   合并成一个阶段不是更简单吗？
 *   （提示：想想如果以后要支持 PREPARE 语句（预编译），分开有什么好处？）
 * ==========================================================================
 */

#include <string.h>

#include "resolve_stage.h"

#include "common/conf/ini.h"
#include "common/io/io.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "event/session_event.h"
#include "event/sql_event.h"
#include "session/session.h"
#include "sql/stmt/stmt.h"

using namespace common;

/**
 * ★ 语义解析入口
 *
 * 做两件事：
 *   1. 查当前数据库（必须在某个 DB 下才能执行 SQL）
 *   2. 调用 Stmt::create_stmt() 把语法树转换成语句对象
 *
 * @return RC::SCHEMA_DB_NOT_EXIST 没有选择数据库
 *         RC::SUCCESS 语义解析成功，stmt 已存入事件
 *
 * 💡 提问：如果数据库不存在，为什么不在 ParseStage 更早失败？
 *   （提示：Parse 阶段还没拿到 db 对象，它只管语法不管数据库状态）
 */
RC ResolveStage::handle_request(SQLStageEvent *sql_event)
{
  RC            rc            = RC::SUCCESS;
  SessionEvent *session_event = sql_event->session_event();
  SqlResult    *sql_result    = session_event->sql_result();

  ParsedSqlNode *sql_node = sql_event->sql_node().get();

  // Commands that do not require a currently-selected database
  bool needs_db = true;
  switch (sql_node->flag) {
    case SCF_SHOW_DATABASES:
    case SCF_CREATE_DATABASE:
    case SCF_DROP_DATABASE:
    case SCF_USE_DATABASE:
      needs_db = false;
      break;
    default:
      break;
  }

  // ★ 获取当前数据库
  // MiniOB 目前只支持单数据库，"当前库"在 Session 中保存
  Db *db = nullptr;
  if (needs_db) {
    db = session_event->session()->get_current_db();
    if (nullptr == db) {
      LOG_ERROR("cannot find current db");
      rc = RC::SCHEMA_DB_NOT_EXIST;
      sql_result->set_return_code(rc);
      sql_result->set_state_string("no db selected");
      return rc;
    }
  }

  // ★ 工厂方法：根据 AST 类型创建对应的 Stmt 对象
  // Stmt::create_stmt() 内部是一个 switch-case，根据 ParsedSqlNode::flag 分发：
  //   SCF_SELECT → new SelectStmt (会调 db->find_table() 验证表存在)
  //   SCF_INSERT → new InsertStmt
  //   SCF_CREATE_TABLE → new CreateTableStmt
  //   ...
  Stmt *stmt = nullptr;

  rc = Stmt::create_stmt(db, *sql_node, stmt);
  if (rc != RC::SUCCESS && rc != RC::UNIMPLEMENTED) {
    LOG_WARN("failed to create stmt. rc=%d:%s", rc, strrc(rc));
    sql_result->set_return_code(rc);
    return rc;
  }

  // ★ 将语句对象存入事件，给下一个阶段（OptimizeStage）使用
  sql_event->set_stmt(stmt);

  return rc;
}
