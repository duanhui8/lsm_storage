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
 * ★【核心学习文件】cli_communicator.cpp — 命令行交互模式实现
 * ==========================================================================
 *
 * ★ CLI 模式是 MiniOB 的"交互式 SQL 终端"，类似 mysql 命令行客户端。
 *
 * ★ 核心技术：GNU Readline 库（通过 MiniobLineReader 封装）
 *   readline 提供：
 *   - 行编辑（左右移动光标、删除字符）
 *   - 历史记录（上下箭头回显之前的命令）
 *   - Tab 补全（如果配置了的话）
 *   - 持久化历史到 .miniob.history 文件
 *
 * ★ 数据流：
 *   stdin → MiniobLineReader::my_readline() → SQL 字符串 → SessionEvent
 *   stdout ← BufferedWriter ← PlainCommunicator::write_result()
 *
 * ★ MiniobLineReader 是 common/linereader/line_reader.h 中定义的封装。
 *   它在 GNU readline 库基础上增加了：
 *   - 退出命令检测（".exit" / "exit"）
 *   - 空白行过滤
 *   - 历史文件管理
 *
 * 💡 提问：为什么 CLI 模式不需要 poll/event loop 而 NetServer 需要？
 *   （提示：CLI 模式的 readline() 是阻塞调用，用户没按回车就一直等。
 *          而 NetServer 需要同时处理多个连接的 I/O，必须用 poll 做多路复用。
 *          单用户 vs 多用户，这是本质区别）
 * ==========================================================================
 */

#include "net/cli_communicator.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "event/session_event.h"
#include "net/buffered_writer.h"
#include "session/session.h"
#include "common/linereader/line_reader.h"

#define MAX_MEM_BUFFER_SIZE 8192
#define PORT_DEFAULT 6789

using common::MiniobLineReader;

const std::string LINE_HISTORY_FILE = "./.miniob.history";

/**
 * ★ init — CLI 模式初始化
 *
 * 关键步骤：
 * 1. 调用 PlainCommunicator::init() — 基础初始化（创建 writer_ 等）
 * 2. 检测 fd == STDIN_FILENO — 确认是 stdin 模式
 * 3. 替换 writer_ — 指向 stdout 而非 socket
 * 4. 初始化 MiniobLineReader — 启用 readline 库（行编辑 + 历史记录）
 * 5. 修改消息分隔符 — '\0' → '\n'（终端模式用换行结束每条消息）
 * 6. 设置 fd_ = -1 — 防止父类析构函数 close(STDIN_FILENO)
 *
 * ★ 为什么重新创建 writer_？
 *  PlainCommunicator::init 创建的 writer_ 指向 socket fd，
 *  但 CLI 模式下没有 socket，输出应该写到 stdout。
 *
 * ★ fd_ = -1 是一个"防御性编程"技巧：
 *  Communicator::~Communicator() 会 close(fd_)。
 *  如果 fd_ 还是 STDIN_FILENO(0)，析构时会 close(0)，
 *  导致程序失去标准输入。把 fd_ 设为 -1，close(-1) 是无害的（返回 EBADF）。
 */
RC CliCommunicator::init(int fd, unique_ptr<Session> session, const string &addr)
{
  RC rc = PlainCommunicator::init(fd, std::move(session), addr);
  if (OB_FAIL(rc)) {
    LOG_WARN("fail to init communicator", strrc(rc));
    return rc;
  }

  if (fd == STDIN_FILENO) {
    write_fd_ = STDOUT_FILENO;
    delete writer_;
    writer_ = new BufferedWriter(write_fd_);

    // ★ 初始化 readline，指定历史文件路径
    MiniobLineReader::instance().init(LINE_HISTORY_FILE);

    // ★ CLI 模式用换行 '\n' 作为消息分隔符（而不是 socket 模式的 '\0'）
    const char delimiter = '\n';
    send_message_delimiter_.assign(1, delimiter);

    fd_ = -1;  // 防止被父类析构函数关闭
  } else {
    rc = RC::INVALID_ARGUMENT;
    LOG_WARN("only stdin supported");
  }
  return rc;
}

/**
 * ★ read_event — 从 stdin 读取一行 SQL
 *
 * 使用 MiniobLineReader::my_readline() 是在 readline 基础上封装的。
 * 它显示 "miniob > " 提示符，等待用户输入，按回车后返回输入的字符串。
 *
 * ★ 三种特殊情况：
 *   1. 空字符串 — 用户按了空回车，跳过
 *   2. 空白行 — 只有空格/制表符，无实际内容，跳过
 *   3. 退出命令 — 用户输入 ".exit" 或 "exit"，设置 exit_ = true
 *
 * ★ 退出命令的处理：
 *   read_event 设置 exit_ = true 并返回 SUCCESS（不返回 event）。
 *   上层的 CliServer 主循环调用 exit() 检查，发现为 true 后退出循环。
 *   这种"检查-退出"模式让退出逻辑集中在 Server 层，Communicator 只负责检测。
 */
RC CliCommunicator::read_event(SessionEvent *&event)
{
  event                  = nullptr;
  const char *prompt_str = "miniob > ";
  std::string command    = MiniobLineReader::instance().my_readline(prompt_str);
  if (command.empty()) {
    return RC::SUCCESS;  // 空输入，跳过
  }

  if (common::is_blank(command.c_str())) {
    return RC::SUCCESS;  // 纯空白，跳过
  }

  if (MiniobLineReader::instance().is_exit_command(command)) {
    exit_ = true;        // ★ 用户要退出
    return RC::SUCCESS;
  }

  event = new SessionEvent(this);
  event->set_query(command);
  return RC::SUCCESS;
}

/**
 * ★ write_result — 输出结果到 stdout
 *
 * 直接代理给 PlainCommunicator::write_result()，不做额外处理。
 * 唯一的区别是 writer_ 指向的是 stdout 而不是 socket。
 *
 * ★ need_disconnect 始终设为 false：
 *   CLI 模式下不存在"连接断开"的概念（stdin/stdout 不会断），
 *   所以 need_disconnect 永远返回 false。
 */
RC CliCommunicator::write_result(SessionEvent *event, bool &need_disconnect)
{
  RC rc = PlainCommunicator::write_result(event, need_disconnect);

  need_disconnect = false;
  return rc;
}

/**
 * ★ 析构 — 历史记录会被自动保存
 *
 * MiniobLineReader 在析构时会自动将本次会话的命令历史
 * 保存到 LINE_HISTORY_FILE（./.miniob.history）。
 * 下次启动 CLI 模式时，可以通过上下箭头回显之前的命令。
 */
CliCommunicator::~CliCommunicator() { LOG_INFO("Command history saved to %s", LINE_HISTORY_FILE.c_str()); }
