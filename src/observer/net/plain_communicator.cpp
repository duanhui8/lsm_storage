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
 * ★★★【核心学习文件】plain_communicator.cpp — 火山模型执行驱动器 ★★★
 * ==========================================================================
 *
 * ★ 这个文件包含两个核心逻辑：
 *
 *   1. read_event() — 从 socket 读 SQL 文本（网络 I/O 协议解析）
 *   2. write_result_internal() — ★ 火山模型执行驱动器 ★
 *      这是 "Result → Volcano → Text" 转换的核心：
 *        open() → 输出列头 → while(next_tuple()) 输出数据 → close()
 *
 * ★ write_result_internal 是理解 MiniOB 执行模型的关键：
 *   它调用 sql_result->open() 触发整个算子树初始化，
 *   然后用 while 循环不断调用 sql_result->next_tuple() 拉取结果行，
 *   最后 sql_result->close() 清理资源。
 *   这就是"火山模型"（Volcano Model）的驱动端。
 *
 * ★ 数据流向：
 *   StorageEngine        open()/next() 调用方向 ↓
 *   ↑ 返回 Tuple 流
 *   TableScan  ←  Predicate  ←  Project  ←  SqlResult::next_tuple()
 *                                                  ↑
 *                            PlainCommunicator::write_tuple_result() 在这里！
 *
 * 💡 综合思考题：如果表有 1 亿行，while(next_tuple()) 循环会执行 1 亿次，
 *    这在 MiniOB 中有什么问题？真正的数据库怎么解决？
 *    （提示：内存分配、网络发送次数、上下文切换、
 *            真实 DB 使用批量发送 + 游标分页）
 * ==========================================================================
 */

#include "net/plain_communicator.h"
#include "common/io/io.h"
#include "common/log/log.h"
#include "event/session_event.h"
#include "net/buffered_writer.h"
#include "session/session.h"
#include "sql/expr/tuple.h"

PlainCommunicator::PlainCommunicator()
{
  // ★ 初始化消息分隔符为 '\0'
  // CliCommunicator 会覆盖为 '\n'（因为 stdin 模式用换行结束）
  send_message_delimiter_.assign(1, '\0');

  // ★ 调试信息前缀为 "# "，客户端可以根据这个前缀过滤调试行
  debug_message_prefix_.resize(2);
  debug_message_prefix_[0] = '#';
  debug_message_prefix_[1] = ' ';
}

/**
 * ★ read_event — 从 socket 读取一个完整的 SQL 请求
 *
 * 协议规则：
 *   - 消息以 '\0' 字节结束
 *   - 最大长度 8192 字节（max_packet_size）
 *   - 非阻塞模式下，数据没到齐时返回 nullptr（不是错误）
 *
 * ★ 读取循环的关键逻辑：
 *   1. ::read(fd, buf, remaining) — 直接读 socket
 *   2. 检查 EAGAIN — 非阻塞 I/O 下没数据可读，continue 等待
 *   3. 扫描数据中的 '\0' — 找到则消息完整
 *   4. 超长保护 — 超过 max_packet_size 返回 IOERR_TOO_LONG
 *   5. 连接关闭检测 — read_len == 0 返回 IOERR_CLOSE
 *
 * ★ 为什么把 '\0' 之后的数据丢弃？
 *   MiniOB 当前只支持"一发一收"模式（one request, one response），
 *   不处理 pipeline 请求（多个请求一次性发过来）。
 *
 * 💡 提问：如果客户端发送 "SELECT * FROM t1\0DROP TABLE t1\0"，
 *   第二条 SQL 会被执行吗？
 *   （提示：看代码 — '\0' 后的数据被丢弃了，第二条不会执行。
 *          这是安全设计：防止 SQL 注入式的批量命令执行）
 *
 * 💡 提问：read_len == 0 为什么表示"对端关闭"而 read_len < 0 是错误？
 *   （提示：TCP 协议：read 返回 0 表示 FIN（对方正常关闭连接），
 *          -1 表示错误（需要检查 errno 判断具体原因）。EAGAIN 不是错误）
 */
RC PlainCommunicator::read_event(SessionEvent *&event)
{
  RC rc = RC::SUCCESS;

  event = nullptr;  // ★ 默认没有完整事件

  int data_len = 0;
  int read_len = 0;

  const int    max_packet_size = 8192;
  vector<char> buf(max_packet_size);

  // ★ 循环读取直到遇到 '\0' 或缓冲区满或连接关闭
  while (true) {
    read_len = ::read(fd_, buf.data() + data_len, max_packet_size - data_len);
    if (read_len < 0) {
      if (errno == EAGAIN) {
        // ★ 非阻塞 I/O：数据还没到，等下次 poll 触发再读
        continue;
      }
      break;  // 真正的错误，退出循环
    }
    if (read_len == 0) {
      // ★ 对方关闭了连接（TCP FIN）
      break;
    }

    if (read_len + data_len > max_packet_size) {
      // ★ 超大包保护：不是马上拒绝，而是继续记录长度
      data_len += read_len;
      break;
    }

    // ★ 扫描新读入的数据，找 '\0' 结束符
    bool msg_end = false;
    for (int i = 0; i < read_len; i++) {
      if (buf[data_len + i] == 0) {
        data_len += i + 1;
        msg_end = true;
        break;
      }
    }

    if (msg_end) {
      break;  // 完整消息已收到
    }

    data_len += read_len;
  }

  // ★ 错误处理：按优先级判断
  if (data_len > max_packet_size) {
    LOG_WARN("The length of sql exceeds the limitation %d", max_packet_size);
    return RC::IOERR_TOO_LONG;
  }
  if (read_len == 0) {
    LOG_INFO("The peer has been closed %s", addr());
    return RC::IOERR_CLOSE;
  } else if (read_len < 0) {
    LOG_ERROR("Failed to read socket of %s, %s", addr(), strerror(errno));
    return RC::IOERR_READ;
  }

  // ★ 组装 SessionEvent — 包含 SQL 文本和 Communicator 引用
  LOG_INFO("receive command(size=%d): %s", data_len, buf.data());
  event = new SessionEvent(this);
  event->set_query(string(buf.data()));
  return rc;
}

/**
 * ★ write_state — 输出执行状态
 *
 * 这是向客户端汇报"SQL 执行成功还是失败"的入口。
 *
 * state_string 非空时：输出 "RC > message"（如 "FAILURE > table not found"）
 * state_string 空时：  输出 "SUCCESS" 或 "FAILURE"
 *
 * ★ BufferedWriter::writen 中的 n 代表"完整写入"（write all），
 *   不同于原始的 ::write 可能只写入部分字节。
 *   BufferedWriter 内部会循环调用 ::write 直到所有数据写完。
 */
RC PlainCommunicator::write_state(SessionEvent *event, bool &need_disconnect)
{
  SqlResult    *sql_result   = event->sql_result();
  const int     buf_size     = 2048;
  char         *buf          = new char[buf_size];
  const string &state_string = sql_result->state_string();
  if (state_string.empty()) {
    const char *result = RC::SUCCESS == sql_result->return_code() ? "SUCCESS" : "FAILURE";
    snprintf(buf, buf_size, "%s\n", result);
  } else {
    snprintf(buf, buf_size, "%s > %s\n", strrc(sql_result->return_code()), state_string.c_str());
  }

  RC rc = writer_->writen(buf, strlen(buf));
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to send data to client. err=%s", strerror(errno));
    need_disconnect = true;
    delete[] buf;
    return RC::IOERR_WRITE;
  }

  need_disconnect = false;
  delete[] buf;

  return RC::SUCCESS;
}

/**
 * ★ write_debug — 输出调试信息
 *
 * 以 "# " 前缀逐行输出 sql_debug 中的所有调试信息。
 * 调试信息包括但不限于：优化器选择的执行计划、索引使用情况、执行时间等。
 * 每条调试信息后跟换行符。
 *
 * 仅在 session_->sql_debug_on() 为 true 时才输出。
 * 调试功能通过 SQL 命令打开：SET sql_debug = 1
 */
RC PlainCommunicator::write_debug(SessionEvent *request, bool &need_disconnect)
{
  if (!session_->sql_debug_on()) {
    return RC::SUCCESS;
  }

  SqlDebug &sql_debug = request->sql_debug();

  const list<string> &debug_infos = sql_debug.get_debug_infos();
  for (auto &debug_info : debug_infos) {
    RC rc = writer_->writen(debug_message_prefix_.data(), debug_message_prefix_.size());
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to send data to client. err=%s", strerror(errno));
      need_disconnect = true;
      return RC::IOERR_WRITE;
    }

    rc = writer_->writen(debug_info.data(), debug_info.size());
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to send data to client. err=%s", strerror(errno));
      need_disconnect = true;
      return RC::IOERR_WRITE;
    }

    char newline = '\n';

    rc = writer_->writen(&newline, 1);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to send new line to client. err=%s", strerror(errno));
      need_disconnect = true;
      return RC::IOERR_WRITE;
    }
  }

  need_disconnect = false;
  return RC::SUCCESS;
}

/**
 * ★ write_result — 输出完整响应
 *
 * 响应格式（纯文本协议）：
 *   1. 列头行（如果有列的话）
 *   2. 数据行
 *   3. 状态行（SUCCESS/FAILURE）
 *   4. 调试信息（如果 sql_debug 开启，以 "# " 开头）
 *   5. 消息分隔符（'\0'）
 *
 * ★ 输出顺序很重要：
 *   - 先写数据，再写调试，最后写结束符
 *   - 结束符让客户端知道"消息完整了，可以解析了"
 *
 * ★ writer_->flush() — 强制刷新缓冲区
 *   数据可能还在 BufferedWriter 的缓冲区中，flush() 确保发送到网络。
 *   TODO 表示这里没有处理 flush 的错误 — 一个已知的不完善之处。
 */
RC PlainCommunicator::write_result(SessionEvent *event, bool &need_disconnect)
{
  RC rc = write_result_internal(event, need_disconnect);
  if (!need_disconnect) {
    RC rc1 = write_debug(event, need_disconnect);
    if (OB_FAIL(rc1)) {
      LOG_WARN("failed to send debug info to client. rc=%s, err=%s", strrc(rc), strerror(errno));
    }
  }
  if (!need_disconnect) {
    // ★ 发送消息结束符，告知客户端"这条消息完了"
    rc = writer_->writen(send_message_delimiter_.data(), send_message_delimiter_.size());
    if (OB_FAIL(rc)) {
      LOG_ERROR("Failed to send data back to client. ret=%s, error=%s", strrc(rc), strerror(errno));
      need_disconnect = true;
      return rc;
    }
  }
  writer_->flush();  // TODO handle error
  return rc;
}

/**
 * ★★★ write_result_internal — 核心：火山模型执行驱动器 ★★★
 *
 * 这是理解 MiniOB 如何"执行"SQL 的关键函数。
 * 它展示了火山模型的标准驱动模式：
 *
 *   sql_result->open()          ← 初始化整个算子树（向下调用到 TableScan::open()）
 *   输出列头（schema）           ← 从 sql_result->tuple_schema() 获取
 *   while (next_tuple(tuple)) { ← 逐行拉取（火山模型的核心循环）
 *     格式化并输出当前行          ← Tuple::cell_at(i, value) 获取每个字段
 *   }
 *   sql_result->close()         ← 清理资源
 *
 * ★ 关键：SqlResult 是一个"包装器"，
 *   它内部持有 PhysicalOperator 树，并把物理算子的接口"翻译"成结果集的接口。
 *   上层（Communicator）不需要知道底层是 TableScan + Predicate + Project，
 *   只需要调用 open / next_tuple / close。
 *
 * ★ 没有列头时（cell_num == 0）的特殊处理：
 *   INSERT/DELETE 等非查询语句没有输出列，
 *   直接输出执行状态（"SUCCESS" 或 "FAILURE > reason"）
 *
 * 💡 提问：为什么先输出列头再进入 while 循环？
 *   （提示：列头是固定的，只需要输出一次。
 *          而数据行数量未知（可能 0 行，可能 100 万行），
 *          客户端需要先解析列头才能正确解析后续的每一行数据。
 *          如果先输出数据行再输出列头，客户端就没法实时解析了）
 *
 * 💡 提问：如果 sql_result->open() 失败了，还有必要 close() 吗？
 *   （提示：看 open 失败后的处理 — 立即 close() + write_state。
 *          open() 可能部分成功（比如分配了部分资源后失败），
 *          close() 负责安全清理，避免资源泄漏）
 */
RC PlainCommunicator::write_result_internal(SessionEvent *event, bool &need_disconnect)
{
  RC rc = RC::SUCCESS;

  need_disconnect = true;

  SqlResult *sql_result = event->sql_result();

  // ★ 路径 1：执行失败 或 没有算子（DDL/控制命令）→ 直接输出状态
  if (RC::SUCCESS != sql_result->return_code() || !sql_result->has_operator()) {
    return write_state(event, need_disconnect);
  }

  // ★ 路径 2：正常查询 → 打开算子树
  rc = sql_result->open();
  if (OB_FAIL(rc)) {
    sql_result->close();
    sql_result->set_return_code(rc);
    return write_state(event, need_disconnect);
  }

  // ★ 输出列头
  const TupleSchema &schema   = sql_result->tuple_schema();
  const int          cell_num = schema.cell_num();

  for (int i = 0; i < cell_num; i++) {
    const TupleCellSpec &spec  = schema.cell_at(i);
    const char          *alias = spec.alias();
    if (nullptr != alias || alias[0] != 0) {
      // ★ 用 " | " 分隔列名，与数据行的分隔符保持一致
      if (0 != i) {
        const char *delim = " | ";
        rc = writer_->writen(delim, strlen(delim));
        if (OB_FAIL(rc)) {
          LOG_WARN("failed to send data to client. err=%s", strerror(errno));
          return rc;
        }
      }

      int len = strlen(alias);
      rc = writer_->writen(alias, len);
      if (OB_FAIL(rc)) {
        LOG_WARN("failed to send data to client. err=%s", strerror(errno));
        sql_result->close();
        return rc;
      }
    }
  }

  // ★ 列头后换行
  if (cell_num > 0) {
    char newline = '\n';
    rc = writer_->writen(&newline, 1);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to send data to client. err=%s", strerror(errno));
      sql_result->close();
      return rc;
    }
  }

  // Chunk mode removed — always tuple result
  rc = write_tuple_result(sql_result);

  if (OB_FAIL(rc)) {
    return rc;
  }

  // ★ 没有任何输出列的处理（INSERT/DELETE 等）
  if (cell_num == 0) {
    // 除了select之外，其它的消息通常不会通过operator来返回结果，表头和行数据都是空的
    // 这里针对这种情况做特殊处理，当表头和行数据都是空的时候，就返回处理的结果
    // 可能是insert/delete等操作，不直接返回给客户端数据，这里把处理结果返回给客户端
    RC rc_close = sql_result->close();
    if (rc == RC::SUCCESS) {
      rc = rc_close;
    }
    sql_result->set_return_code(rc);
    return write_state(event, need_disconnect);
  } else {
    need_disconnect = false;
  }

  RC rc_close = sql_result->close();
  if (OB_SUCC(rc)) {
    rc = rc_close;
  }

  return rc;
}

/**
 * ★★★ write_tuple_result — 火山模型的标准驱动循环 ★★★
 *
 * 这是 SQL 执行的核心输出循环：
 *
 *   while (RC::SUCCESS == (rc = sql_result->next_tuple(tuple))) {
 *     // 输出一行数据
 *     for (int i = 0; i < cell_num; i++) {
 *       tuple->cell_at(i, value);       // 获取第 i 列的值
 *       writer_->writen(value.to_string());  // 格式化为字符串并发送
 *     }
 *   }
 *   // rc == RECORD_EOF 表示数据已全部读完（正常结束）
 *
 * ★ 输出格式：
 *   每行：col1 | col2 | col3\n
 *   列之间用 " | " 分隔，行以 '\n' 结尾
 *
 * ★ RC::RECORD_EOF 不是错误：
 *   这是火山模型的"文件结束"信号。当 next_tuple() 返回 RECORD_EOF 时，
 *   说明算子树的数据已经全部输出完成，循环正常退出。
 *
 * ★ 性能注意：
 *   每一列的值都通过 value.to_string() 转换成字符串再发送。
 *   如果结果集有 1 亿行 × 10 列，这个循环会产生大量字符串对象。
 *   生产级数据库会使用列式批量编码（如 Apache Arrow 格式）来避免这个问题。
 *
 * 💡 提问：如果 next_tuple() 返回 RC::SUCCESS 但 tuple 是 nullptr，
 *   代码会崩溃吗？
 *   （提示：assert(tuple != nullptr) 在 debug 模式下会终止，
 *          在 release 模式下会直接解引用 nullptr 导致 segfault。
 *          这是"信任内部代码"的设计：如果算子树实现正确，不可能出现这种情况）
 */
RC PlainCommunicator::write_tuple_result(SqlResult *sql_result)
{
  RC rc = RC::SUCCESS;
  Tuple *tuple = nullptr;
  while (RC::SUCCESS == (rc = sql_result->next_tuple(tuple))) {
    assert(tuple != nullptr);

    int cell_num = tuple->cell_num();
    for (int i = 0; i < cell_num; i++) {
      if (i != 0) {
        const char *delim = " | ";
        rc = writer_->writen(delim, strlen(delim));
        if (OB_FAIL(rc)) {
          LOG_WARN("failed to send data to client. err=%s", strerror(errno));
          sql_result->close();
          return rc;
        }
      }

      Value value;
      rc = tuple->cell_at(i, value);
      if (rc != RC::SUCCESS) {
        LOG_WARN("failed to get tuple cell value. rc=%s", strrc(rc));
        sql_result->close();
        return rc;
      }

      string cell_str = value.to_string();

      rc = writer_->writen(cell_str.data(), cell_str.size());
      if (OB_FAIL(rc)) {
        LOG_WARN("failed to send data to client. err=%s", strerror(errno));
        sql_result->close();
        return rc;
      }
    }

    char newline = '\n';
    rc = writer_->writen(&newline, 1);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to send data to client. err=%s", strerror(errno));
      sql_result->close();
      return rc;
    }
  }

  if (rc == RC::RECORD_EOF) {
    rc = RC::SUCCESS;  // ★ EOF 是正常结束，不是错误
  }
  return rc;
}

/**
 * ★ write_chunk_result — Chunk 模式输出（向量化）
 *
 * 与 Tuple 模式的区别：
 *   Tuple 模式：一次获取一行，一行一行循环
 *   Chunk 模式：一次获取一批行（Chunk），先按列组织再逐行输出
 *
 * Chunk 是列式存储的一批数据：
 *   col_num = 3, rows = 1024
 *   chunk 内部按列存储：[col0_data[1024], col1_data[1024], col2_data[1024]]
 *
 * 向量化执行的优势（理论上的，MiniOB 实现较基础）：
 *   - CPU 缓存友好（同列数据连续存储）
 *   - 可以减少虚函数调用次数
 *   - 可以利用 SIMD 指令优化
 *
 * 💡 提问：看输出代码，Chunk 模式和外层 Tuple 模式在输出格式上有区别吗？
 *   （提示：没有区别 — 都是 "col | col | col\n" 格式。
 *          Chunk 的优化在执行层面（next_chunk 一次获取一批），
 *          对输出格式没有影响）
 */
RC PlainCommunicator::write_chunk_result(SqlResult *)
{
  // Chunk mode removed with adapter layer
  return RC::UNIMPLEMENTED;
#if 0
  RC rc = RC::SUCCESS;
  Chunk chunk;
  while (RC::SUCCESS == (rc = sql_result->next_chunk(chunk))) {
    int col_num = chunk.column_num();
    for (int row_idx = 0; row_idx < chunk.rows(); row_idx++) {
      for (int col_idx = 0; col_idx < col_num; col_idx++) {
        if (col_idx != 0) {
          const char *delim = " | ";
          rc = writer_->writen(delim, strlen(delim));
          if (OB_FAIL(rc)) {
            LOG_WARN("failed to send data to client. err=%s", strerror(errno));
            sql_result->close();
            return rc;
          }
        }

        Value value = chunk.get_value(col_idx, row_idx);
        string cell_str = value.to_string();

        rc = writer_->writen(cell_str.data(), cell_str.size());
        if (OB_FAIL(rc)) {
          LOG_WARN("failed to send data to client. err=%s", strerror(errno));
          sql_result->close();
          return rc;
        }
      }
      char newline = '\n';
      rc = writer_->writen(&newline, 1);
      if (OB_FAIL(rc)) {
        LOG_WARN("failed to send data to client. err=%s", strerror(errno));
        sql_result->close();
        return rc;
      }
    }
    chunk.reset();  // ★ 释放当前 Chunk 的内存，准备接收下一批
  }

  if (rc == RC::RECORD_EOF) {
    rc = RC::SUCCESS;
  }
  return rc;
#endif
}
