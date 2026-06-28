/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/ob_log_base_header.h */

#ifndef OCEANBASE_LOGSERVICE_OB_LOG_BASE_HEADER_
#define OCEANBASE_LOGSERVICE_OB_LOG_BASE_HEADER_

#include <cstdint>
#include <cstring>
#include "ob_log_base_type.h"

namespace oceanbase {
namespace logservice {

/**
 * ObLogBaseHeader — Every log entry starts with this 20-byte header.
 * This is the first thing written into each LogEntry payload.
 * Simplified from OB 4.4.2 ob_log_base_header.h.
 */
struct ObLogBaseHeader {
  static const int16_t BASE_HEADER_VERSION = 1;

  // Replay barrier flags (from OB 4.4.2)
  static const int32_t NO_NEED_BARRIER        = 0;
  static const int32_t NEED_PRE_REPLAY_BARRIER  = 0x01;  // Must wait for all prior logs
  static const int32_t NEED_POST_REPLAY_BARRIER = 0x02;  // Later logs must wait for this
  static const int32_t PAYLOAD_IS_COMPRESSED     = 0x04;
  static const int32_t STRICT_BARRIER = NEED_PRE_REPLAY_BARRIER | NEED_POST_REPLAY_BARRIER;

  int16_t  version_;       // BASE_HEADER_VERSION = 1
  int16_t  log_type_;      // ObLogBaseType
  int32_t  flag_;          // barrier / compressed flags
  int64_t  replay_hint_;   // opaque hint for replay ordering

  ObLogBaseHeader() : version_(BASE_HEADER_VERSION),
                       log_type_(INVALID_LOG_BASE_TYPE),
                       flag_(NO_NEED_BARRIER),
                       replay_hint_(0) {}

  ObLogBaseHeader(ObLogBaseType type, int32_t flag = NO_NEED_BARRIER)
      : version_(BASE_HEADER_VERSION), log_type_(static_cast<int16_t>(type)),
        flag_(flag), replay_hint_(0) {}

  void reset() {
    version_ = BASE_HEADER_VERSION;
    log_type_ = INVALID_LOG_BASE_TYPE;
    flag_ = NO_NEED_BARRIER;
    replay_hint_ = 0;
  }

  bool is_valid() const { return version_ == BASE_HEADER_VERSION; }

  int serialize(char *buf, int64_t buf_len, int64_t &pos) const {
    if (buf_len - pos < get_serialize_size()) return -1;
    std::memcpy(buf + pos, this, get_serialize_size());
    pos += get_serialize_size();
    return 0;
  }
  int deserialize(const char *buf, int64_t data_len, int64_t &pos) {
    if (data_len - pos < get_serialize_size()) return -1;
    std::memcpy(this, buf + pos, get_serialize_size());
    pos += get_serialize_size();
    return 0;
  }
  static int64_t get_serialize_size() { return sizeof(ObLogBaseHeader); }
};

}  // namespace logservice
}  // namespace oceanbase

#endif
