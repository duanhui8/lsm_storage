//
// Created by Wangyunlai on 2024/01/10.
//

/**
 * ==========================================================================
 * 【核心文件】SqlTaskHandler — SQL 请求完整处理流水线（实现）
 * ==========================================================================
 *
 * ★ 这是"一条 SQL 从收到网络数据到返回结果"的完整实现。
 *
 * 两个方法，两段流水线：
 *
 *   handle_event() — 外流水线：读事件 → 处理SQL → 写结果
 *   handle_sql()   — 内流水线：缓存 → 解析 → 语义 → 优化 → 执行
 *
 * 外流水线（handle_event）:
 *   1. communicator->read_event()     — 从网络/终端读 SQL 文本
 *   2. session_stage_.handle_request2() — 关联会话上下文
 *   3. handle_sql()                   — ★ 核心：5 阶段 SQL 处理
 *   4. communicator->write_result()   — 将结果写回客户端
 *   5. 检查 need_disconnect          — 连接是否可用
 *
 * 内流水线（handle_sql）:
 *   QueryCache → Parse → Resolve → Optimize → Execute
 *
 * ★ 数据传递：
 *   SQLStageEvent 是各阶段的共享上下文（"流水线托盘"），
 *   每个阶段往上面放自己的产出物，下一阶段从上面取。
 *   事件对象传递路径: sql_node → stmt → logical_operator → physical_operator
 *
 * 💡 提问：read_event 返回 nullptr 但 RC 是 SUCCESS，
 *   这是什么情况？为什么不能简单返回一个错误？
 *   （提示：非阻塞 I/O 模式下，可能没有完整的一行数据可读，
 *         需要等下次 poll 触发再读。在 CLI 模式下呢？）
 *
 * 💡 提问：为什么 handle_sql 失败后还要继续调用 write_result？
 *   （提示：错误信息也要告诉客户端，不能悄无声息地失败）
 * ==========================================================================
 */

#include "net/sql_task_handler.h"
#include "net/communicator.h"
#include "event/session_event.h"
#include "event/sql_event.h"
#include "session/session.h"

/**
 * ★ handle_event — 一次 SQL 请求的完整处理
 *
 * 这个函数在 CliServer 的 while 循环中反复调用（单线程模式），
 * 或者在 ThreadHandler 的工作线程中调用（多线程模式）。
 *
 * 处理流程：
 *  1. 读 — communicator->read_event() 阻塞或非阻塞地读取 SQL
 *  2. 处理 — handle_sql() 执行 5 阶段流水线
 *  3. 写 — communicator->write_result() 发送结果
 *  4. 检查连接状态 — need_disconnect 判断是否需要断连
 *  5. 清理 — delete event，重置 session 状态
 *
 * ★ need_disconnect 和 RC 的分工：
 *   RC 告诉你"write 这个操作"有没有成功，
 *   need_disconnect 告诉你"这条连接"还能不能继续用。
 *   比如：write 写入 100 字节成功（RC=SUCCESS），但写第 101 字节时发现
 *   客户端断开了 → need_disconnect=true。
 *
 * 💡 提问：delete event 在函数末尾执行，如果 write_result 很慢（网络拥塞），
 *   这个 event 对象什么时候被释放？内存占用会持续多久？
 *   （提示：每个连接一次只处理一个请求，event 的生命周期 = 一次 handle_event 调用）
 */
RC SqlTaskHandler::handle_event(Communicator *communicator)
{
  SessionEvent *event = nullptr;

  // ★ 第1步：从网络读 SQL 文本
  // read_event 内部做：读数据 → 解析协议头 → 组装成 SessionEvent
  RC rc = communicator->read_event(event);
  if (OB_FAIL(rc)) {
    return rc;  // 读失败（网络错误等），直接返回
  }

  if (nullptr == event) {
    return RC::SUCCESS;  // 没有完整请求（非阻塞模式下正常）
  }

  // ★ 第2步：关联会话
  session_stage_.handle_request2(event);

  // ★ 第3步：创建 SQLStageEvent — "流水线托盘"
  // 这个对象贯穿 parse → resolve → optimize → execute 四个阶段
  SQLStageEvent sql_event(event, event->query());

  // ★ 第4步：执行 5 阶段 SQL 处理流水线
  rc = handle_sql(&sql_event);
  if (OB_FAIL(rc)) {
    LOG_TRACE("failed to handle sql. rc=%s", strrc(rc));
    // 注意：即使处理失败，也会把错误码写入 sql_result，然后通过 write_result 返回给客户端
    event->sql_result()->set_return_code(rc);
  }

  // ★ 第5步：将结果写回客户端
  // need_disconnect 是输出参数，告诉调用方"连接是否断了"
  bool need_disconnect = false;
  rc = communicator->write_result(event, need_disconnect);
  LOG_INFO("write result return %s", strrc(rc));

  // ★ 第6步：清理会话状态
  event->session()->set_current_request(nullptr);
  Session::set_current_session(nullptr);

  // ★ 第7步：释放事件对象（一个 event = 一次请求 = 一个 SQL）
  delete event;

  // ★ 第8步：根据 need_disconnect 决定是否继续服务这个连接
  if (need_disconnect) {
    return RC::INTERNAL;  // 返回 INTERNAL 告诉上层（Server）断开此连接
  }
  return RC::SUCCESS;
}

/**
 * ★ handle_sql — 5 阶段 SQL 处理流水线
 *
 * ┌──────────────┬──────────────────┬─────────────────────────────────────┐
 * │ 阶段         │ 输入             │ 输出                                │
 * ├──────────────┼──────────────────┼─────────────────────────────────────┤
 * │ QueryCache   │ SQL 字符串       │ 缓存命中则返回，miss 则跳过         │
 * │ ParseStage   │ SQL 字符串       │ ParsedSqlNode（语法树）             │
 * │ ResolveStage │ ParsedSqlNode    │ Stmt（语句对象，表/列已绑定）       │
 * │ OptimizeStage│ Stmt             │ PhysicalOperator（物理执行计划）    │
 * │ ExecuteStage │ PhysicalOperator │ SqlResult（写入 sql_event）         │
 * └──────────────┴──────────────────┴─────────────────────────────────────┘
 *
 * ★ 错误传播：任何阶段失败都直接返回错误码。
 *   但 optimize 有个特殊处理：返回 UNIMPLEMENTED 不算失败，
 *   因为 DDL 语句没有对应的优化操作，跳过即可。
 *
 * 💡 提问：QueryCache 是怎么判断"命中"的？
 *   SQL 字符串完全相等？还是会把 "SELECT * FROM t1" 和 "select * from t1" 当同一个？
 *   （提示：看 query_cache_stage.cpp 的实现，它比较的是字符串还是规范化后的查询）
 *
 * 💡 提问：如果优化器耗时很长（复杂查询的 CBO），这个函数会阻塞多久？
 *   在 NetServer 的多线程模式下（one-thread-per-connection），
 *   阻塞只影响当前连接，不影响其他连接。但在 CliServer 的单线程模式下呢？
 */
RC SqlTaskHandler::handle_sql(SQLStageEvent *sql_event)
{
  // 阶段1：查询缓存
  RC rc = query_cache_stage_.handle_request(sql_event);
  if (OB_FAIL(rc)) {
    LOG_TRACE("failed to do query cache. rc=%s", strrc(rc));
    return rc;
  }

  // 阶段2：词法+语法解析 → AST
  rc = parse_stage_.handle_request(sql_event);
  if (OB_FAIL(rc)) {
    LOG_TRACE("failed to do parse. rc=%s", strrc(rc));
    return rc;
  }

  // 阶段3：语义解析 → Stmt（表名/列名绑定）
  rc = resolve_stage_.handle_request(sql_event);
  if (OB_FAIL(rc)) {
    LOG_TRACE("failed to do resolve. rc=%s", strrc(rc));
    return rc;
  }

  // 阶段4：查询优化 → PhysicalOperator
  // ★ 特殊处理：UNIMPLEMENTED 表示"不需要优化"（DDL 语句），不是错误
  rc = optimize_stage_.handle_request(sql_event);
  if (rc != RC::UNIMPLEMENTED && rc != RC::SUCCESS) {
    LOG_TRACE("failed to do optimize. rc=%s", strrc(rc));
    return rc;
  }

  // 阶段5：执行
  rc = execute_stage_.handle_request(sql_event);
  if (OB_FAIL(rc)) {
    LOG_TRACE("failed to do execute. rc=%s", strrc(rc));
    return rc;
  }

  return rc;
}
