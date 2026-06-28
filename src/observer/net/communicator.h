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
// Created by Wangyunlai on 2022/11/17.
//

/**
 * ==========================================================================
 * ★★★【核心学习文件】communicator.h — 通讯抽象层 ★★★
 * ==========================================================================
 *
 * ★ 定位：抽象"与客户端通讯"这个概念。
 *   MiniOB 支持多种通讯方式，但上层 SQL 处理逻辑不需要关心具体是哪种。
 *   这就是"策略模式"（Strategy Pattern）的体现。
 *
 *   继承层次：
 *   Communicator (抽象接口)
 *   ├── PlainCommunicator  — 纯文本协议，'\0' 作为消息结束符
 *   │   └── CliCommunicator — 命令行交互模式（stdin/stdout）
 *   └── MysqlCommunicator   — MySQL 协议（兼容 MySQL 客户端连接）
 *
 *   工厂模式：
 *   CommunicatorFactory::create(protocol) → 创建对应的 Communicator 子类
 *
 * ★ 生命周期：
 *   1. Server::accept() 收到新连接 → CommunicatorFactory 创建
 *   2. Communicator::init(fd, session, addr) 初始化
 *   3. 每次有数据 → read_event() 读取请求
 *   4. 处理完成后 → write_result() 发送响应
 *   5. 连接关闭 → 析构
 *
 * ★ 协议无关设计：
 *   上层代码（SqlTaskHandler）只看到 Communicator 接口，
 *   不知道底层是纯文本协议还是 MySQL 协议。
 *   这让 MiniOB 可以同时支持多种客户端：
 *   - 自定义客户端（纯文本协议，简单高效）
 *   - MySQL 客户端（MySQL 协议，兼容性好）
 *   - 命令行交互（stdin/stdout，调试方便）
 *
 * 💡 提问：如果 MiniOB 以后要支持 PostgreSQL 协议（wire protocol），
 *   只需要做什么？需要修改 SqlTaskHandler 吗？
 *   （提示：只需要新增一个 PgCommunicator 子类，实现 read_event/write_result，
 *          SqlTaskHandler 完全不用改 — 这就是面向接口编程的好处）
 * ==========================================================================
 */

#pragma once

#include "common/sys/rc.h"
#include "common/lang/string.h"
#include "common/lang/memory.h"

struct ConnectionContext;
class SessionEvent;
class Session;
class BufferedWriter;

/**
 * ==========================================================================
 * ★ Communicator — 通讯抽象基类
 * ==========================================================================
 *
 * 职责：封装与一个客户端的完整通讯生命周期。
 * 每个客户端连接对应一个 Communicator 对象。
 *
 * 关键方法：
 *   read_event()   — 从 socket 读数据，解析协议，组装 SessionEvent
 *   write_result() — 将 SqlResult 按协议格式写回客户端
 *
 * ★ need_disconnect 输出参数的含义：
 *   write_result() 的返回值 RC 告诉你"写入操作"本身是否成功，
 *   need_disconnect 告诉你"这条连接"还能不能用。
 *   例如：write 了 100 字节成功（RC=SUCCESS），但 write 第 101 字节时
 *   发现客户端断开了 → need_disconnect=true。
 *
 * ★ 为什么 need_disconnect 是输出参数而不是返回值？
 *   因为 RC 已经是返回值了。C++ 中要返回两个值有三种方式：
 *   1. pair/tuple 返回 → 不够语义化
 *   2. 输出参数 → 兼容性好，调用方能明确知道被修改
 *   3. 自定义 Result 类型 → 太重
 *   MiniOB 选择了最轻量的方案：输出引用参数。
 */
class Communicator
{
public:
  virtual ~Communicator();

  /**
   * ★ init — 连接建立后的初始化
   *
   * 参数 fd 是 accept() 返回的 socket 文件描述符。
   * session 是当前会话对象（包含用户状态、变量等）。
   * addr 是对端地址（客户端 IP:Port），用于日志和审计。
   *
   * 注意：fd 的生命周期由 Communicator 管理，
   * 析构时会 close(fd)。
   */
  virtual RC init(int fd, unique_ptr<Session> session, const string &addr);

  /**
   * ★ read_event — 从网络读取并解析一个完整的请求
   *
   * 这是"协议解析"的核心方法。
   * 不同的协议有不同的消息边界：
   *   - PlainCommunicator: 读取直到 '\0' 字节
   *   - MySQL 协议: 读取标准 MySQL packet header + payload
   *
   * ★ 返回 nullptr + RC::SUCCESS 的含义：
   *   在非阻塞 I/O 模式下，socket 可能还没收到完整消息。
   *   此时 event=nullptr，上层跳过处理，等待下次 poll 触发。
   *   在阻塞模式下（CLI 模式），不会出现这种情况。
   *
   * 💡 提问：如果客户端恶意发送一个巨大的包（如 10GB），
   *   read_event 会一直读到 OOM 吗？
   *   （提示：看 PlainCommunicator::read_event 中的 max_packet_size 限制）
   */
  virtual RC read_event(SessionEvent *&event) = 0;

  /**
   * ★ write_result — 将执行结果写回客户端
   *
   * 按照协议格式输出：
   *   - 输出列头（schema）
   *   - 输出每一行数据
   *   - 输出状态信息（SUCCESS/FAILURE）
   *   - 如果是调试模式，输出调试信息（以 '#' 开头）
   *
   * @param need_disconnect [out] 是否需要断开此连接
   *   - true: 连接已不可用，上层的 Server 应断开并清理
   *   - false: 连接正常，可以继续接收下一个请求
   */
  virtual RC write_result(SessionEvent *event, bool &need_disconnect) = 0;

  Session *session() const { return session_.get(); }
  const char *addr() const { return addr_.c_str(); }
  int fd() const { return fd_; }

protected:
  unique_ptr<Session> session_;      ///< 关联的会话（用户状态、变量、事务上下文）
  string              addr_;         ///< 对端地址（仅用于日志）
  BufferedWriter     *writer_ = nullptr;  ///< 缓冲写入器（减少系统调用）
  int                 fd_     = -1;       ///< socket 文件描述符
};

/**
 * ★ CommunicateProtocol — 通讯协议枚举
 *
 * 三种协议对应三种 Communicator 子类。
 * 通过命令行参数或配置文件选择：
 *   miniob --protocol=plain   （默认，自定义纯文本协议）
 *   miniob --protocol=mysql   （MySQL 兼容协议）
 *   miniob --protocol=cli     （命令行交互模式，不监听端口）
 */
enum class CommunicateProtocol
{
  PLAIN,  ///< 以'\0'结尾的纯文本协议
  CLI,    ///< 命令行交互协议（stdin/stdout）
  MYSQL,  ///< MySQL 通讯协议
};

/**
 * ★ CommunicatorFactory — 通讯协议工厂
 *
 * 根据协议类型创建对应的 Communicator 对象。
 * 这是"简单工厂模式"（Simple Factory）的典型应用。
 *
 * 💡 提问：为什么用工厂而不是让调用方直接 new PlainCommunicator()？
 *   （提示：解耦。Server 只需要知道枚举值，不需要 include 各个子类的头文件。
 *          如果要新增协议，只改工厂方法即可）
 */
class CommunicatorFactory
{
public:
  Communicator *create(CommunicateProtocol protocol);
};
