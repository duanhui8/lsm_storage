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
 * ★【核心学习文件】cli_communicator.h — 命令行交互模式通讯器
 * ==========================================================================
 *
 * ★ 定位：继承 PlainCommunicator，覆盖 init/read_event/write_result
 *   来实现基于 stdin/stdout 的命令行交互模式。
 *
 * ★ 使用场景：`./miniob` 直接启动（不带参数），进入交互式 CLI：
 *     miniob >
 *     miniob > SELECT * FROM t1;
 *     id | name
 *     1  | Alice
 *     SUCCESS
 *     miniob >
 *
 * ★ 与 PlainCommunicator 的区别：
 *   |                  | PlainCommunicator     | CliCommunicator         |
 *   |------------------|-----------------------|-------------------------|
 *   | 输入             | socket + '\0' 结束    | stdin + '\n' 结束       |
 *   | 输出             | socket                 | stdout                  |
 *   | 消息分隔符       | '\0'                   | '\n'                    |
 *   | 是否有提示符     | 无                    | "miniob > "             |
 *   | 支持行编辑       | 无                    | 是（readline 库）       |
 *   | 历史记录         | 无                    | 是（.miniob.history）   |
 *
 * ★ 设计注意：
 *   CliCommunicator 继承 PlainCommunicator 有点"实现继承"的味道，
 *   而不是纯粹的接口继承。这意味着如果 PlainCommunicator 改了实现细节，
 *   CliCommunicator 可能受影响。作者在注释中也承认了这一点：
 *   "CLI 不应该是一种协议，只是一种通讯的方式而已"
 *   更好的设计可能是用组合（Communicator + Reader/Writer 接口）。
 *
 * 💡 提问：为什么 CliCommunicator 把 fd_ 设为 -1，把 write_fd_ 设为 STDOUT_FILENO？
 *   （提示：stdin/stdout 不是 socket，不能用 ::close() 关闭。
 *          设置 fd_ = -1 防止父类析构函数 close(STDIN_FILENO)，
 *          因为关闭标准输入会导致程序后续无法读取任何输入）
 */
#pragma once

#include "net/plain_communicator.h"

class CliCommunicator : public PlainCommunicator
{
public:
  CliCommunicator() = default;
  virtual ~CliCommunicator();

  RC init(int fd, unique_ptr<Session> session, const string &addr) override;
  RC read_event(SessionEvent *&event) override;
  RC write_result(SessionEvent *event, bool &need_disconnect) override;

  /**
   * ★ exit() — 查询是否收到了退出命令
   *
   * 当用户输入 ".exit" 或 "exit" 时，read_event 设置 exit_=true。
   * 上层的 CliServer 检查这个标志来决定是否退出主循环。
   *
   * 这是一种"带内信令"（in-band signaling）：
   * 退出信号通过普通的 read_event → exit() 路径传递，
   * 而不是通过额外的信号通道。
   */
  bool exit() const { return exit_; }

private:
  bool exit_ = false;   ///< 是否需要退出 CLI 模式

  /**
   * ★ write_fd_ — 输出文件描述符
   *
   * 普通的 PlainCommunicator 用同一个 fd 读写（socket 是双向的）。
   * 但 CLI 模式下，输入是 STDIN_FILENO(0)，输出是 STDOUT_FILENO(1)，
   * 它们是不同的文件描述符。write_fd_ 专门用于输出。
   */
  int write_fd_ = -1;
};
