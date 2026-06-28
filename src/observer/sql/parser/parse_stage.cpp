/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
*/

//
// Created by Longda on 2021/4/13.
//

/**
 * ==========================================================================
 * 【架构概览】ParseStage — SQL 词法+语法解析阶段
 * ==========================================================================
 *
 * 把一条 SQL 文本转换成 AST（抽象语法树）ParsedSqlNode。
 *
 * SQL 字符串 → flex 词法分析 → Token 序列 → bison 语法分析 → ParsedSqlNode
 *
 * 文件分工：
 *   lex_sql.l    — 词法规则（正则 → Token）
 *   yacc_sql.y   — 语法规则（Token序列 → AST）
 *   parse.h/cpp  — flex+bison 生成的（或被手写的 recursive descent 替换）
 *
 * ★ 词法分析（lex）做什么：
 *   "SELECT * FROM t1 WHERE id > 10"
 *   → [SELECT] [STAR] [FROM] [ID: t1] [WHERE] [ID: id] [GT] [NUMBER: 10]
 *   把字符串切成 Token，每个 Token 有类型和值
 *
 * ★ 语法分析（yacc）做什么：
 *   [SELECT] [STAR] [FROM] [ID: t1] [WHERE] [ID: id] [GT] [NUMBER: 10]
 *   → ParsedSqlNode { type=SCF_SELECT, 列=[*], 表=[t1], 条件=[id > 10] }
 *   根据语法规则把 Token 序列组合成树状结构
 *
 * 💡 提问：为什么解析阶段查不出"表不存在"、"列名写错"这类错误？
 *   解析只做语法检查，不管表和列是否存在。表和列的绑定在 ResolveStage 做。
 *   （提示：试试 SELECT * FROM not_exist_table — 解析会成功，执行才会失败）
 * ==========================================================================
 */

#include <string.h>

#include "parse_stage.h"

#include "common/conf/ini.h"
#include "common/io/io.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "event/session_event.h"
#include "event/sql_event.h"
#include "sql/parser/parse.h"

using namespace common;

/**
 * ★ 解析阶段入口
 *
 * @return RC::SUCCESS  解析成功，AST 已存入事件
 *         RC::SQL_SYNTAX 语法错误（比如 SELECT 拼成了 SELEC）
 *         RC::INTERNAL  空输入（不是错误但无需后续处理）
 */
RC ParseStage::handle_request(SQLStageEvent *sql_event)
{
  RC rc = RC::SUCCESS;

  SqlResult   *sql_result = sql_event->session_event()->sql_result();
  const string &sql        = sql_event->sql();

  // ★ 调用词法+语法分析器
  // parse() 是由 flex+bison 生成的函数，输入 C 字符串，输出 ParsedSqlNode 列表
  ParsedSqlResult parsed_sql_result;
  parse(sql.c_str(), &parsed_sql_result);

  // 情况1：空输入 — 用户只敲了回车或空白字符
  // 返回 RC::INTERNAL 让上层跳过后续阶段（不是错误，只是无事可做）
  if (parsed_sql_result.sql_nodes().empty()) {
    sql_result->set_return_code(RC::SUCCESS);
    sql_result->set_state_string("");
    return RC::INTERNAL;
  }

  // 情况2：多条 SQL — MiniOB 只处理第一条，其余丢弃
  // ★ 设计取舍：单语句模型比多语句简单得多
  //    多语句需要：状态机跟踪"当前在第几条"、错误回滚已执行的语句
  if (parsed_sql_result.sql_nodes().size() > 1) {
    LOG_WARN("got multi sql commands but only 1 will be handled");
  }

  // ★ 关键：把 AST 从临时结果中移动出来
  // unique_ptr 独占所有权，std::move 转移而非拷贝（零开销）
  // 不 move 的话 parsed_sql_result 析构会释放 AST，后面阶段白干了
  unique_ptr<ParsedSqlNode> sql_node = std::move(parsed_sql_result.sql_nodes().front());

  // 情况3：解析过程中发现语法错误
  // lex/yacc 遇到无法匹配的 token 时会设置 SCF_ERROR
  if (sql_node->flag == SCF_ERROR) {
    rc = RC::SQL_SYNTAX;
    sql_result->set_return_code(rc);
    sql_result->set_state_string("Failed to parse sql");
    return rc;
  }

  // ★ 将 AST 存入事件，给下一个阶段（ResolveStage）使用
  sql_event->set_sql_node(std::move(sql_node));

  return RC::SUCCESS;
}
