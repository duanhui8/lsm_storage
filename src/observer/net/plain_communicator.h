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
// Created by Wangyunlai on 2023/06/25.
//

/**
 * ==========================================================================
 * ★【核心学习文件】plain_communicator.h — 纯文本协议通讯器
 * ==========================================================================
 *
 * ★ 协议格式：
 *   请求：SQL 文本 + '\0' 字节（C 风格字符串，用 '\0' 作为结束标志）
 *   响应：列头行 + 数据行 + 状态行 + '\0'
 *
 *   例（SELECT id, name FROM t1）：
 *   响应 →
 *     id | name\n          ← 列头
 *     1 | Alice\n          ← 数据行
 *     2 | Bob\n            ← 数据行
 *     SUCCESS\n            ← 状态
 *     \0                   ← 结束符
 *
 * ★ 为什么用 '\0' 而不是换行或长度前缀？
 *   - 简单：C 字符串天然以 '\0' 结尾
 *   - 缺点：SQL 文本中不能包含 '\0'（实际 SQL 文本不会含 '\0'）
 *   - 对比 MySQL 协议：用固定长度的包头（3 字节 payload 长度 + 1 字节序列号）
 *
 * ★ PlainCommunicator 继承 Communicator，实现了纯文本协议的读写。
 *   CliCommunicator 继承 PlainCommunicator，复用大部分逻辑，
 *   只覆盖了 init()/read_event() 来适配 stdin/stdout。
 */
#pragma once

#include "net/communicator.h"
#include "common/lang/vector.h"

class SqlResult;

class PlainCommunicator : public Communicator
{
public:
  PlainCommunicator();
  virtual ~PlainCommunicator() = default;

  RC read_event(SessionEvent *&event) override;
  RC write_result(SessionEvent *event, bool &need_disconnect) override;

private:
  /**
   * ★ write_state — 输出最终状态（SUCCESS/FAILURE）
   *
   * 无论是查询成功还是失败，最后都输出一行状态信息给客户端。
   * 如果 state_string 非空，则输出 "RC > message" 格式（如 "FAILURE > table not found"）
   * 否则输出 "SUCCESS" 或 "FAILURE"
   */
  RC write_state(SessionEvent *event, bool &need_disconnect);

  /**
   * ★ write_debug — 输出调试信息
   *
   * 调试信息以 '#' 开头，用于在交互模式下显示额外的诊断信息。
   * 例如：优化器选择的索引、执行时间等。
   * 仅在 session 的 sql_debug 开关打开时才输出。
   *
   * 💡 提问：为什么调试信息用 '#' 前缀？
   *   （提示：'#' 在交互客户端中被识别为注释/元数据行，
   *          客户端可以决定显示或隐藏这些行，不影响正常结果解析）
   */
  RC write_debug(SessionEvent *event, bool &need_disconnect);

  /**
   * ★ write_result_internal — 结果输出的核心逻辑
   *
   * 流程：
   *   1. 如果有 operator（DML 语句），先 open()
   *   2. 输出列头（schema 中的 alias）
   *   3. 循环 next_tuple() 输出每一行
   *   4. close() 并输出状态
   *
   * 这是"火山模型驱动循环"的所在：
   *  while (RC::SUCCESS == sql_result->next_tuple(tuple)) { ... }
   */
  RC write_result_internal(SessionEvent *event, bool &need_disconnect);

  /**
   * ★ write_tuple_result — 按行输出结果（Tuple-at-a-time）
   *
   * 每行用 " | " 分隔各列，每行以 '\n' 结尾。
   * 这是传统的行模式输出。
   */
  RC write_tuple_result(SqlResult *sql_result);

  /**
   * ★ write_chunk_result — 按块输出结果（Chunk-at-a-time）
   *
   * Chunk 是列式存储的一批行，比逐行输出效率更高。
   * 这是向量化执行模式下的输出方式。
   *
   * 💡 提问：Tuple 模式和 Chunk 模式有什么区别？
   *   Tuple: 一行一行处理，火山模型的经典方式
   *   Chunk: 一批一批处理，向量化执行，一次处理 1024 行（典型块大小）
   */
  RC write_chunk_result(SqlResult *sql_result);

protected:
  vector<char> send_message_delimiter_;  ///< 发送消息分隔符（默认 '\0'）
  vector<char> debug_message_prefix_;    ///< 调试信息前缀（"# "）
};
