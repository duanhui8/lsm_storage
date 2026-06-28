/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
*/

// __CR__

/*
 *  Created on: Mar 11, 2012
 *      Author: Longda Feng
 */

/**
 * ==========================================================================
 * 【架构概览】main.cpp — MiniOB Observer 启动入口
 * ==========================================================================
 *
 * 这个文件是整个数据库服务端的入口。它完成三件事：
 *   1. 解析命令行参数（端口、协议、存储引擎等）
 *   2. 初始化全局资源（日志、BufferPool、存储引擎）
 *   3. 创建并启动网络服务器，进入事件循环
 *
 * 启动后的处理链路：
 *   客户端连接 → Server::accept() → 创建 Session → 读 SQL → 解析 → 执行 → 返回结果
 *
 * ★ 亮点：通过 -E 参数可以在 Heap 和 LSM 两个存储引擎之间切换，这在真实数据库
 *    中是做不到的（比如 MySQL 不能换个参数就从 InnoDB 切换到 MyRocks）。
 *    这个设计让 MiniOB 成为学习存储引擎对比的理想平台。
 * ==========================================================================
 */

#include <netinet/in.h>
#include <unistd.h>

#include "common/ini_setting.h"
#include "common/init.h"
#include "common/lang/iostream.h"
#include "common/lang/string.h"
#include "common/lang/map.h"
#include "common/os/process.h"
#include "common/os/signal.h"
#include "common/log/log.h"
#include "net/server.h"
#include "net/server_param.h"

using namespace common;

#define NET "NET"

// ★ 设计要点：全局唯一的 Server 指针
// 为什么用全局变量而不是在 main 里用局部变量？
// → 因为信号处理函数 quit_signal_handle 需要访问 server 来调用 shutdown()，
//    而信号处理函数的签名是固定的 void(int)，没法传自定义参数。
//    💡 提问：如果要避免全局变量，有什么替代方案？
static Server *g_server = nullptr;

/**
 * ==========================================================================
 * 命令行参数说明
 * ==========================================================================
 * -p PORT    : TCP 监听端口（默认从配置读取）
 * -P PROTO   : 协议类型 {plain, mysql, cli}
 *              plain = 自定义文本协议（简单）
 *              mysql = 标准 MySQL 协议（可以用 mysql 客户端连接）
 *              cli   = 直接在终端交互（调试最方便）
 * -f FILE    : 配置文件路径
 * -t MODEL   : 事务模型 {vacuous, mvcc}
 *              vacuous = 空事务（无并发控制，单线程场景）
 *              mvcc    = 多版本并发控制
 * -E ENGINE  : 存储引擎 {heap, lsm}
 * -n SIZE    : Buffer Pool 内存大小（字节）
 * -T MODEL   : 线程模型 {one-thread-per-connection, java-thread-pool}
 * -d         : 启用磁盘持久化
 *
 * 💡 提问：vacuous 事务模型下，多个客户端同时修改同一行会发生什么？
 * 💡 提问：为什么 miniob 要支持多种协议，而真实数据库通常只支持一种？
 */
void usage()
{
  cout << "Usage " << endl;
  cout << "-p: server port. if not specified, the item in the config file will be used" << endl;
  cout << "-f: path of config file." << endl;
  cout << "-s: use unix socket and the argument is socket address" << endl;
  cout << "-P: protocol. {plain(default), mysql, cli}." << endl;
  cout << "-t: transaction model. {vacuous(default), mvcc}." << endl;
  cout << "-T: thread handling model. {one-thread-per-connection(default),java-thread-pool}." << endl;
  cout << "-n: buffer pool memory size in byte" << endl;
  cout << "-d: durbility mode. {vacuous(default), disk}" << endl;
  cout << "-E: storage engine. {ob(default), heap, lsm}" << endl;
}

/**
 * ==========================================================================
 * 参数解析 — parse_parameter()
 * ==========================================================================
 *
 * 使用 POSIX 标准 getopt() 解析命令行参数。
 *
 * ★ 设计亮点：ProcessParam 是全局单例（the_process_param()），
 *    整个程序任何地方都可以获取启动参数，不需要层层传递。
 *    这是一种"配置中心"模式，比 Spring 那种依赖注入更轻量。
 *
 * 💡 提问：全局单例有什么缺点？在单元测试中怎么处理？
 * ==========================================================================
 */
void parse_parameter(int argc, char **argv)
{
  string process_name = get_process_name(argv[0]);

  ProcessParam *process_param = the_process_param();  // 获取全局单例

  process_param->init_default(process_name);

  int          opt;
  extern char *optarg;
  while ((opt = getopt(argc, argv, "dp:P:s:t:T:f:o:e:E:hn:")) > 0) {
    switch (opt) {
      case 's': process_param->set_unix_socket_path(optarg); break;
      case 'p': process_param->set_server_port(atoi(optarg)); break;
      case 'P': process_param->set_protocol(optarg); break;
      case 'f': process_param->set_conf(optarg); break;
      case 'o': process_param->set_std_out(optarg); break;
      case 'e': process_param->set_std_err(optarg); break;
      case 't': process_param->set_trx_kit_name(optarg); break;
      case 'E': process_param->set_storage_engine(optarg); break;  // ★ 存储引擎选择
      case 'T': process_param->set_thread_handling_name(optarg); break;
      case 'n': process_param->set_buffer_pool_memory_size(atoi(optarg)); break;
      case 'd': process_param->set_durability_mode("disk"); break;
      case 'h':
        usage();
        exit(0);
        return;
      default: cout << "Unknown option: " << static_cast<char>(opt) << ", ignored" << endl; break;
    }
  }
}

/**
 * ==========================================================================
 * 服务器初始化 — init_server()
 * ==========================================================================
 *
 * 这里完成服务器配置的组装：
 *   1. 从配置文件读取网络参数（监听地址、最大连接数、端口）
 *   2. 命令行参数覆盖配置文件（命令行优先级更高）
 *   3. 根据协议选择不同的 Server 实现：
 *      - CliServer：标准输入输出，适合调试（F5 调试时用这个）
 *      - NetServer：TCP 网络监听，适合真实客户端连接
 *
 * ★ 设计模式：策略模式（Strategy Pattern）
 *    CliServer 和 NetServer 都继承自 Server 基类，
 *    通过 server_param.use_std_io 选择具体策略。
 *
 * 💡 提问：为什么 TCP 模式和 CLI 模式要用不同的 Server 类？
 *    （提示：想想数据从哪里读、结果往哪里写）
 * ==========================================================================
 */
Server *init_server()
{
  map<string, string> net_section = get_properties()->get(NET);

  ProcessParam *process_param = the_process_param();

  long listen_addr        = INADDR_ANY;              // 监听所有网卡
  long max_connection_num = MAX_CONNECTION_NUM_DEFAULT;
  int  port               = PORT_DEFAULT;

  // 从配置文件读取网络参数
  map<string, string>::iterator it = net_section.find(CLIENT_ADDRESS);
  if (it != net_section.end()) {
    string str = it->second;
    str_to_val(str, listen_addr);
  }

  it = net_section.find(MAX_CONNECTION_NUM);
  if (it != net_section.end()) {
    string str = it->second;
    str_to_val(str, max_connection_num);
  }

  // 命令行参数覆盖配置文件（命令行 > 配置文件 > 默认值）
  if (process_param->get_server_port() > 0) {
    port = process_param->get_server_port();
    LOG_INFO("Use port config in command line: %d", port);
  } else {
    it = net_section.find(PORT);
    if (it != net_section.end()) {
      string str = it->second;
      str_to_val(str, port);
    }
  }

  ServerParam server_param;
  server_param.listen_addr        = listen_addr;
  server_param.max_connection_num = max_connection_num;
  server_param.port               = port;

  // ★ 核心分支：根据协议选择 Server 类型
  if (0 == strcasecmp(process_param->get_protocol().c_str(), "mysql")) {
    server_param.protocol = CommunicateProtocol::MYSQL;  // 标准 MySQL 协议
  } else if (0 == strcasecmp(process_param->get_protocol().c_str(), "cli")) {
    server_param.use_std_io = true;   // ★ CLI 模式：直接从 stdin 读 SQL，stdout 写结果
    server_param.protocol   = CommunicateProtocol::CLI;
  } else {
    server_param.protocol = CommunicateProtocol::PLAIN;  // 自定义简文本协议
  }

  if (process_param->get_unix_socket_path().size() > 0 && !server_param.use_std_io) {
    server_param.use_unix_socket  = true;
    server_param.unix_socket_path = process_param->get_unix_socket_path();
  }
  server_param.thread_handling = process_param->thread_handling_name();

  // 根据 use_std_io 选择策略
  Server *server = nullptr;
  if (server_param.use_std_io) {
    server = new CliServer(server_param);  // CLI 模式（调试用）
  } else {
    server = new NetServer(server_param);  // TCP 网络模式
  }

  return server;
}

/**
 * ==========================================================================
 * 信号处理 — 优雅退出
 * ==========================================================================
 *
 * ★ 重要设计：信号处理函数里不能做重活
 *
 * 为什么 quit_signal_handle 只是创建了一个线程，而不是直接调 g_server->shutdown()？
 * → 因为收到信号时，主线程可能正持有锁（比如写日志的锁），
 *    如果在信号处理函数里直接调 shutdown()，shutdown 里又要拿同一把锁 → 死锁！
 *
 * 所以正确的做法是：信号处理函数只创建一个新线程来做实际的清理工作。
 * 这个线程是全新的、没持有任何锁，可以安全地执行 shutdown。
 *
 * 💡 提问：set_signal_handler(nullptr) 这行是什么意思？为什么要这样做？
 *    （提示：用户连续按两次 Ctrl+C 会怎样？）
 * ==========================================================================
 */
void *quit_thread_func(void *_signum)
{
  intptr_t signum = (intptr_t)_signum;
  LOG_INFO("Receive signal: %ld", signum);
  if (g_server) {
    g_server->shutdown();  // 实际关闭逻辑在新线程中执行
  }
  return nullptr;
}

void quit_signal_handle(int signum)
{
  // 防止多次调用退出：
  // 如果用户连续按 Ctrl+C，第一次触发退出后先把 handler 置空，
  // 第二次调用时 set_signal_handler 已经无效，不会创建多个退出线程
  set_signal_handler(nullptr);

  pthread_t tid;
  pthread_create(&tid, nullptr, quit_thread_func, (void *)(intptr_t)signum);
}

const char *startup_tips = R"(
Welcome to the OceanBase database implementation course.

Copyright (c) 2021 OceanBase and/or its affiliates.

Learn more about OceanBase at https://github.com/oceanbase/oceanbase
Learn more about MiniOB at https://github.com/oceanbase/miniob

)";

/**
 * ==========================================================================
 * main() — 程序入口（第 183 行）
 * ==========================================================================
 *
 * 主函数只有 4 个步骤，非常简洁：
 *   1. set_signal_handler  — 注册 Ctrl+C 处理
 *   2. parse_parameter      — 解析命令行参数
 *   3. init()               — 初始化全局资源（日志、BufferPool、存储引擎等）
 *   4. init_server + serve  — 创建服务器并进入事件循环
 *   5. cleanup              — 退出前清理资源
 *
 * ★ 设计思想：main 函数是"导演"，只负责编排，具体工作委托给各模块。
 *    好的 main 函数应该像目录一样，一眼能看清程序做了什么。
 *
 * 💡 提问：init() 失败后为什么直接 return，而 serve() 正常结束后还要调 cleanup？
 *    init() 里面分配的资源不需要清理吗？
 *    （提示：想想 init() 内部失败时是否已经做了自己的清理）
 * ==========================================================================
 */
int main(int argc, char **argv)
{
  int rc = STATUS_SUCCESS;

  cout << startup_tips;

  // 第1步：注册信号处理，Ctrl+C 能优雅退出
  set_signal_handler(quit_signal_handle);

  // 第2步：解析参数
  parse_parameter(argc, argv);

  // 第3步：初始化所有全局组件
  // ★ init() 内部做了什么？这是理解整个系统启动流程的关键
  // 建议断点进入 init() 查看：日志系统、BufferPool、存储引擎的初始化
  rc = init(the_process_param());
  if (rc != STATUS_SUCCESS) {
    cerr << "Shutdown due to failed to init!" << endl;
    cleanup();
    return rc;
  }

  // 第4步：创建服务器并开始监听
  g_server = init_server();
  g_server->serve();  // ★ 这里进入事件循环，阻塞直到收到退出信号

  LOG_INFO("Server stopped");

  // 第5步：清理
  cleanup();

  delete g_server;
  return 0;
}
