/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/ob_log_handler.cpp */

#include "ob_log_handler.h"
#include "ob_log_service.h"
#include <cstring>
#include <cstdlib>

namespace oceanbase {
namespace logservice {

ObLogHandler::ObLogHandler()
    : ls_id_(-1), palf_handle_impl_(nullptr), is_inited_(false) {}

ObLogHandler::~ObLogHandler()
{
  // PalfHandle does not own the impl — palf_env does
}

int ObLogHandler::init(ObLogService *log_service, int64_t ls_id)
{
  if (is_inited_) return -1;
  ls_id_ = ls_id;

  // Get PalfEnv from LogService and create/get handle
  palf::PalfEnvImpl *palf_env = log_service->get_palf_env();
  if (nullptr == palf_env) return -1;

  int ret = palf_env->create_palf_handle_impl(ls_id,
      palf::AccessMode::APPEND, palf_handle_impl_);
  if (ret != 0) return ret;

  ret = palf_handle_.init(palf_handle_impl_);
  if (ret != 0) return ret;

  is_inited_ = true;
  return 0;
}

int ObLogHandler::register_replay_handler(ObLogBaseType type,
                                           ObIReplaySubHandler *handler)
{
  replay_handlers_[static_cast<int16_t>(type)] = handler;
  return 0;
}

int ObLogHandler::register_role_change_handler(ObLogBaseType type,
                                                ObIRoleChangeSubHandler *handler)
{
  role_change_handlers_[static_cast<int16_t>(type)] = handler;
  return 0;
}

int ObLogHandler::append(const void *buf, int64_t buf_len, ObLogBaseType type,
                          palf::LSN &lsn, int64_t &scn)
{
  if (!is_inited_) return -1;
  if (nullptr == buf || buf_len <= 0) return -1;

  // 1. Build the full log buffer: [ObLogBaseHeader][payload]
  ObLogBaseHeader base_header(type);
  int64_t total_len = ObLogBaseHeader::get_serialize_size() + buf_len;

  char *log_buf = static_cast<char *>(std::malloc(total_len));
  if (nullptr == log_buf) return -1;

  int64_t pos = 0;
  base_header.serialize(log_buf, total_len, pos);
  std::memcpy(log_buf + pos, buf, buf_len);

  // 2. Append to PALF
  palf::PalfAppendOptions opts;
  int ret = palf_handle_.append(opts, log_buf, total_len, 0, lsn, scn);

  std::free(log_buf);
  return ret;
}

int ObLogHandler::replay(const void *buf, int64_t buf_len,
                          const palf::LSN &lsn, int64_t scn)
{
  if (nullptr == buf || buf_len <= 0) return -1;

  // 1. Parse ObLogBaseHeader
  const char *ptr = static_cast<const char *>(buf);
  ObLogBaseHeader base_header;
  int64_t pos = 0;
  int ret = base_header.deserialize(ptr, buf_len, pos);
  if (ret != 0) return ret;

  int16_t log_type = base_header.log_type_;

  // 2. Dispatch to registered handler
  auto it = replay_handlers_.find(log_type);
  if (it == replay_handlers_.end()) {
    // No handler registered — skip (e.g., padding entries)
    return 0;
  }

  const char *payload = ptr + pos;
  int64_t payload_len = buf_len - pos;

  return it->second->replay(payload, payload_len, lsn, scn);
}

int ObLogHandler::switch_to_leader()
{
  for (auto &pair : role_change_handlers_) {
    pair.second->switch_to_leader();
  }
  return 0;
}

int ObLogHandler::switch_to_follower()
{
  for (auto &pair : role_change_handlers_) {
    pair.second->switch_to_follower_gracefully();
  }
  return 0;
}

}  // namespace logservice
}  // namespace oceanbase
