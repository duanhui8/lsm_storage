/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
*/

//
// Created by Wangyunlai on 2024/01/10.
//

#pragma once

/**
 * ==========================================================================
 * 【架构概览】SqlTaskHandler — SQL 请求处理的总调度器
 * ==========================================================================
 *
 * 这个类是 MiniOB 中"一条 SQL 从接收到返回结果"的完整流水线编排者。
 * 它组合了 6 个处理阶段（Stage），按顺序执行：
 *
 *   read_event() → SessionStage → QueryCache → Parse → Resolve → Optimize → Execute → write_result()
 *
 * ======================== 一条 SQL 的完整旅程 ==============================
 *
 * 用户在客户端输入: SELECT * FROM t1 WHERE id > 10;
 *
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │  阶段          输入                    输出                         │
 * ├─────────────────────────────────────────────────────────────────────┤
 * │  SessionStage  原始SQL字符串("SELEC..") 解析出 SQL 文本，创建 Session │
 * │  QueryCacheStage SQL文本               已缓存的SQL结果（miss则跳过） │
 * │  ParseStage     SQL 字符串              ParsedSqlNode (AST 语法树)   │
 * │  ResolveStage   ParsedSqlNode           Stmt (语义分析后的语句对象)  │
 * │  OptimizeStage  Stmt                    PhysicalOperator (物理执行计划)│
 * │  ExecuteStage   PhysicalOperator        SqlResult (查询结果)         │
 * └─────────────────────────────────────────────────────────────────────┘
 *
 * ★ 关键设计模式：Pipeline（流水线模式）
 *    每个 Stage 只做一件事，上一个 Stage 的输出是下一个 Stage 的输入。
 *    通过 SQLStageEvent 在 Stage 之间传递数据。
 *
 * ★ 设计亮点：为什么 Parse、Resolve、Optimize、Execute 要拆成独立 Stage？
 *    1. 职责单一：每个阶段只关心自己的事
 *    2. 可替换：可以把 OptimizeStage 换成不同的优化策略
 *    3. 可跳过：DDL 语句不需要 optimize 阶段（返回 RC::UNIMPLEMENTED 即可跳过）
 *    4. 可调试：在任一 Stage 打断点，就能看到该阶段的输入和输出
 *
 * 💡 提问：为什么 QueryCache 放在 Parse 之前而不是 Execute 之后？
 *    （提示：想想缓存命中后可以跳过哪些阶段，节省了什么？）
 * ==========================================================================
 */

#include "common/sys/rc.h"
#include "session/session_stage.h"
#include "sql/executor/execute_stage.h"
#include "sql/optimizer/optimize_stage.h"
#include "sql/parser/parse_stage.h"
#include "sql/parser/resolve_stage.h"
#include "sql/query_cache/query_cache_stage.h"

class Communicator;
class SQLStageEvent;

class SqlTaskHandler
{
public:
  SqlTaskHandler()          = default;
  virtual ~SqlTaskHandler() = default;

  /**
   * ★ 主入口：一次请求的完整处理
   *
   * 执行流程：
   *  1. communicator->read_event()  — 从网络读 SQL 文本
   *  2. session_stage_.handle_request2() — 关联 Session
   *  3. handle_sql() — 执行完整的 SQL 处理流水线
   *  4. communicator->write_result() — 将结果写回客户端
   *  5. 检查 need_disconnect — 网络断开了就返回 RC::INTERNAL
   *
   * @param communicator 网络连接对象（负责读写网络数据）
   * @return RC::SUCCESS 继续服务，RC::INTERNAL 断开连接
   *
   * 💡 提问：为什么 need_disconnect 和 RC 要分开？
   *    如果 write_result 返回错误，能不能直接断开连接？
   *    （提示：想想 write 失败一定是网络问题吗？如果是逻辑错误呢？）
   */
  RC handle_event(Communicator *communicator);

  /**
   * ★ SQL 流水线核心
   *
   * 按顺序执行 5 个阶段。注意 optimize_stage 的特殊处理：
   *   rc != RC::UNIMPLEMENTED && rc != RC::SUCCESS 才返回错误
   * 这意味着：如果优化器说 "我处理不了"（UNIMPLEMENTED），可以跳过它，
   * 直接进入执行阶段。这是为 DDL 语句设计的——DDL 不需要查询优化。
   *
   * 💡 提问：为什么不用 if/else 判断语句类型来决定是否跑 optimize，
   *    而是让 optimize 自己返回 UNIMPLEMENTED？
   *    （提示：如果以后加了新类型的语句，哪种方式改动更少？）
   */
  RC handle_sql(SQLStageEvent *sql_event);

private:
  SessionStage    session_stage_;      /// 会话阶段：关联 Session，做权限检查等
  QueryCacheStage query_cache_stage_;  /// 查询缓存：相同查询直接返回缓存结果
  ParseStage      parse_stage_;        /// 词法+语法解析：SQL文本 → AST语法树(ParsedSqlNode)
  ResolveStage    resolve_stage_;      /// 语义解析：AST → 语句对象(Stmt)，如表名列名绑定
  OptimizeStage   optimize_stage_;     /// 查询优化：Stmt → 物理执行计划(PhysicalOperator)
  ExecuteStage    execute_stage_;      /// 执行：PhysicalOperator → 实际读取数据，返回结果
};
