//
// Created by Longda on 2021
//

/**
 * ==========================================================================
 * 【架构概览】Server — 网络服务器层（两个实现）
 * ==========================================================================
 *
 * 这个文件实现了两种服务器模式，通过继承 Server 基类来实现：
 *
 * ┌──────────────────────────────────────────────────────────────────┐
 * │                    Server (抽象基类)                             │
 * │                    serve() / shutdown()                          │
 * ├──────────────────┬───────────────────────────────────────────────┤
 * │   NetServer      │   CliServer                                   │
 * │   TCP/Unix Socket│   标准输入输出（调试模式）                     │
 * │   多线程处理      │   单线程处理                                  │
 * └──────────────────┴───────────────────────────────────────────────┘
 *
 * ★ NetServer 的处理模型：
 *   1. bind + listen 端口
 *   2. poll() 等待新连接
 *   3. accept() 接受连接 → 创建 Communicator → 交给 ThreadHandler
 *   4. ThreadHandler 在线程池中运行 SqlTaskHandler 处理请求
 *
 * ★ CliServer 的处理模型（F5 调试时用这个）：
 *   1. 从 stdin 读 SQL
 *   2. 直接用 SqlTaskHandler 处理
 *   3. 结果写到 stdout
 *   4. 没有网络、没有多线程，调试清晰
 *
 * ★ 设计亮点：为什么 CliServer 不用 ThreadHandler？
 *   调试时如果有多线程，GDB 默认只跟踪当前线程（其他线程还在跑），
 *   单线程模式 -P cli 让断点行为完全可控。
 *
 * 💡 提问：NetServer::serve() 用 poll 而不是 select/epoll，为什么？
 *   （提示：考虑连接数。数据库通常有多少并发连接？
 *          poll vs epoll 在什么场景下性能差异最明显？）
 * ==========================================================================
 */

#include "net/server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>

#include "common/ini_setting.h"
#include "common/io/io.h"
#include "common/lang/mutex.h"
#include "common/log/log.h"
#include "event/session_event.h"
#include "session/session_stage.h"
#include "net/communicator.h"
#include "net/cli_communicator.h"
#include "session/session.h"
#include "net/thread_handler.h"
#include "net/sql_task_handler.h"

using namespace common;

ServerParam::ServerParam()
{
  listen_addr        = INADDR_ANY;               // 默认监听所有网卡
  max_connection_num = MAX_CONNECTION_NUM_DEFAULT;
  port               = PORT_DEFAULT;
}

// ===================== NetServer（TCP 网络模式） ==========================

NetServer::NetServer(const ServerParam &input_server_param) : Server(input_server_param) {}

NetServer::~NetServer()
{
  if (started_) {
    shutdown();
  }
}

int NetServer::set_non_block(int fd)
{
  int flags = fcntl(fd, F_GETFL);
  if (flags == -1) {
    LOG_INFO("Failed to get flags of fd :%d. ", fd);
    return -1;
  }

  flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  if (flags == -1) {
    LOG_INFO("Failed to set non-block flags of fd :%d. ", fd);
    return -1;
  }
  return 0;
}

/**
 * ★ 接受新连接
 *
 * 这是 poll 事件循环的回调：当监听 socket 有新的连接请求时调用。
 * 流程：
 *   1. accept() — 接受 TCP 连接
 *   2. set_non_block() — 设为非阻塞（线程池模型要求）
 *   3. TCP_NODELAY — 禁用 Nagle 算法（减少延迟）
 *   4. CommunicatorFactory::create() — 根据协议类型创建通信对象
 *   5. thread_handler_->new_connection() — 交给线程池处理
 *
 * ★ 为什么设非阻塞？
 *   一个线程可能处理多个连接。如果用阻塞 I/O，一个慢客户端会拖住
 *   整个线程。非阻塞 + 读写时可以立即返回，线程能切换到其他连接。
 *
 * ★ 为什么设 TCP_NODELAY？
 *   Nagle 算法会攒小包延迟发送，降低网络利用率但增加延迟。
 *   数据库场景对延迟敏感，禁用它。
 *
 * 💡 提问：new_connection 如果线程池满了怎么办？
 *    看 ThreadHandler 的实现是怎么处理拒绝的。
 */
void NetServer::accept(int fd)
{
  struct sockaddr_in addr;
  socklen_t          addrlen = sizeof(addr);

  int ret = 0;

  int client_fd = ::accept(fd, (struct sockaddr *)&addr, &addrlen);
  if (client_fd < 0) {
    LOG_ERROR("Failed to accept client's connection, %s", strerror(errno));
    return;
  }

  char ip_addr[24];
  if (inet_ntop(AF_INET, &addr.sin_addr, ip_addr, sizeof(ip_addr)) == nullptr) {
    LOG_ERROR("Failed to get ip address of client, %s", strerror(errno));
    ::close(client_fd);
    return;
  }
  stringstream address;
  address << ip_addr << ":" << addr.sin_port;
  string addr_str = address.str();

  ret = set_non_block(client_fd);
  if (ret < 0) {
    LOG_ERROR("Failed to set socket of %s as non blocking, %s", addr_str.c_str(), strerror(errno));
    ::close(client_fd);
    return;
  }

  if (!server_param_.use_unix_socket) {
    int yes = 1;
    ret     = setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    if (ret < 0) {
      LOG_ERROR("Failed to set socket of %s option as : TCP_NODELAY %s\n", addr_str.c_str(), strerror(errno));
      ::close(client_fd);
      return;
    }
  }

  // ★ 根据协议类型创建 Communicator（策略模式）
  Communicator *communicator = communicator_factory_.create(server_param_.protocol);

  RC rc = communicator->init(client_fd, make_unique<Session>(Session::default_session()), addr_str);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to init communicator. rc=%s", strrc(rc));
    delete communicator;
    return;
  }

  LOG_INFO("Accepted connection from %s\n", communicator->addr());

  // ★ 交给线程池处理
  rc = thread_handler_->new_connection(communicator);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to handle new connection. rc=%s", strrc(rc));
    delete communicator;
    return;
  }
}

int NetServer::start()
{
  if (server_param_.use_std_io) {
    return -1;
  } else if (server_param_.use_unix_socket) {
    return start_unix_socket_server();
  } else {
    return start_tcp_server();
  }
}

int NetServer::start_tcp_server()
{
  int                ret = 0;
  struct sockaddr_in sa;

  server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket_ < 0) {
    LOG_ERROR("socket(): can not create server socket: %s.", strerror(errno));
    return -1;
  }

  int yes = 1;
  ret     = setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  if (ret < 0) {
    LOG_ERROR("Failed to set socket option of reuse address: %s.", strerror(errno));
    ::close(server_socket_);
    return -1;
  }

  ret = set_non_block(server_socket_);
  if (ret < 0) {
    LOG_ERROR("Failed to set socket option non-blocking:%s. ", strerror(errno));
    ::close(server_socket_);
    return -1;
  }

  memset(&sa, 0, sizeof(sa));
  sa.sin_family      = AF_INET;
  sa.sin_port        = htons(server_param_.port);
  sa.sin_addr.s_addr = htonl(server_param_.listen_addr);

  ret = ::bind(server_socket_, (struct sockaddr *)&sa, sizeof(sa));
  if (ret < 0) {
    LOG_ERROR("bind(): can not bind server socket, %s", strerror(errno));
    ::close(server_socket_);
    return -1;
  }

  ret = listen(server_socket_, server_param_.max_connection_num);
  if (ret < 0) {
    LOG_ERROR("listen(): can not listen server socket, %s", strerror(errno));
    ::close(server_socket_);
    return -1;
  }
  LOG_INFO("Listen on port %d", server_param_.port);

  started_ = true;
  LOG_INFO("Observer start success");
  return 0;
}

int NetServer::start_unix_socket_server()
{
  // ... Unix socket 类似 TCP，只是 bind 的是文件路径而不是 IP:端口
  // 省略详细注释，与 TCP 版本类比理解即可
  int ret        = 0;
  server_socket_ = socket(PF_UNIX, SOCK_STREAM, 0);
  if (server_socket_ < 0) {
    LOG_ERROR("socket(): can not create unix socket: %s.", strerror(errno));
    return -1;
  }

  ret = set_non_block(server_socket_);
  if (ret < 0) {
    LOG_ERROR("Failed to set socket option non-blocking:%s. ", strerror(errno));
    ::close(server_socket_);
    return -1;
  }

  unlink(server_param_.unix_socket_path.c_str());

  struct sockaddr_un sockaddr;
  memset(&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sun_family = PF_UNIX;
  snprintf(sockaddr.sun_path, sizeof(sockaddr.sun_path), "%s", server_param_.unix_socket_path.c_str());

  ret = ::bind(server_socket_, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
  if (ret < 0) {
    LOG_ERROR("bind(): can not bind server socket(path=%s), %s", sockaddr.sun_path, strerror(errno));
    ::close(server_socket_);
    return -1;
  }

  ret = listen(server_socket_, server_param_.max_connection_num);
  if (ret < 0) {
    LOG_ERROR("listen(): can not listen server socket, %s", strerror(errno));
    ::close(server_socket_);
    return -1;
  }
  LOG_INFO("Listen on unix socket: %s", sockaddr.sun_path);

  started_ = true;
  LOG_INFO("Observer start success");
  return 0;
}

/**
 * ★ NetServer::serve() — 网络服务器主循环
 *
 * 执行流程：
 *   1. 创建 ThreadHandler（one-thread-per-connection 或 java-thread-pool）
 *   2. 启动服务器 socket（bind + listen）
 *   3. ★ 进入 poll 事件循环：
 *      - poll 等待新连接（timeout 500ms）
 *      - 有连接 → accept → 创建 Communicator → 交给线程池
 *   4. 收到 shutdown 信号 → 停止线程池
 *
 * 💡 提问：poll timeout 为什么是 500ms 而不是 0（立即返回）或 -1（无限等待）？
 *   （提示：started_ 在什么时候被设为 false？如果 poll 无限等待，shutdown 怎么生效？）
 */
int NetServer::serve()
{
  thread_handler_ = ThreadHandler::create(server_param_.thread_handling.c_str());
  if (thread_handler_ == nullptr) {
    LOG_ERROR("Failed to create thread handler: %s", server_param_.thread_handling.c_str());
    return -1;
  }

  RC rc = thread_handler_->start();
  if (OB_FAIL(rc)) {
    LOG_ERROR("failed to start thread handler: %s", strrc(rc));
    return -1;
  }

  int retval = start();
  if (retval == -1) {
    LOG_PANIC("Failed to start network");
    exit(-1);
  }

  // ★ TCP 网络模式的事件循环
  if (!server_param_.use_std_io) {
    struct pollfd poll_fd;
    poll_fd.fd      = server_socket_;
    poll_fd.events  = POLLIN;     // 监听"有数据可读"事件（即新连接到达）
    poll_fd.revents = 0;

    while (started_) {
      int ret = poll(&poll_fd, 1, 500);  // 500ms 超时
      if (ret < 0) {
        LOG_WARN("[listen socket] poll error. fd = %d, ret = %d, error=%s", poll_fd.fd, ret, strerror(errno));
        break;
      } else if (0 == ret) {
        // 超时，没有新连接，继续循环（检查 started_ 标志）
        continue;
      }

      if (poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        LOG_ERROR("poll error. fd = %d, revents = %d", poll_fd.fd, poll_fd.revents);
        break;
      }

      this->accept(server_socket_);  // ★ 接受新连接
    }
  }

  thread_handler_->stop();
  thread_handler_->await_stop();  // 等待所有工作线程退出
  delete thread_handler_;
  thread_handler_ = nullptr;

  started_ = false;
  LOG_INFO("NetServer quit");
  return 0;
}

void NetServer::shutdown()
{
  LOG_INFO("NetServer shutting down");
  started_ = false;  // 设置标志，poll 循环检测到后退出
}

// ===================== CliServer（CLI 交互模式） ==========================

CliServer::CliServer(const ServerParam &input_server_param) : Server(input_server_param) {}

CliServer::~CliServer()
{
  if (started_) {
    shutdown();
  }
}

/**
 * ★ CliServer::serve() — CLI 模式主循环
 *
 * 这是最简单、最直观的执行路径。没有网络、没有多线程：
 *
 *   while (true) {
 *     read_line(stdin);               // 从终端读一行
 *     task_handler.handle_event();    // 解析+优化+执行
 *     write_result(stdout);           // 结果打印到终端
 *   }
 *
 * ★ 为什么 CLI 模式不用 ThreadHandler？
 *   - 只有一个"客户端"（终端），不需要并发
 *   - 调试友好：单线程意味着 GDB 断点行为完全可预期
 *   - 代码简单：学习 MiniOB 的最佳入口
 *
 * 💡 提问：CliServer 和 NetServer 都实现了 serve()，为什么放在同一个文件？
 *   （提示：虽然功能不同，但它们共享了什么？什么把它们关联在一起？）
 */
int CliServer::serve()
{
  CliCommunicator communicator;

  RC rc = communicator.init(STDIN_FILENO, make_unique<Session>(Session::default_session()), "stdin");
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to init cli communicator. rc=%s", strrc(rc));
    return -1;
  }

  started_ = true;

  SqlTaskHandler task_handler;
  while (started_ && !communicator.exit()) {
    rc = task_handler.handle_event(&communicator);  // ★ 一次请求的完整处理
    if (OB_FAIL(rc)) {
      started_ = false;
    }
  }

  started_ = false;
  return 0;
}

void CliServer::shutdown()
{
  LOG_INFO("CliServer shutting down");
  started_ = false;
}
