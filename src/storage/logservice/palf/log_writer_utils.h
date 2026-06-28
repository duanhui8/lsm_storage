/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/log_writer_utils.h */

#ifndef OCEANBASE_LOGSERVICE_LOG_WRITER_UTILS_
#define OCEANBASE_LOGSERVICE_LOG_WRITER_UTILS_
#include <cstring>
#include "log_define.h"

namespace oceanbase {
namespace palf {

/**
 * LogWriteBuf — buffer for log write serialization.
 * Used to assemble LogEntry + LogGroupEntry before disk flush.
 */
class LogWriteBuf {
public:
  LogWriteBuf() : buf_(nullptr), buf_len_(0) {}
  LogWriteBuf(const char *buf, int64_t buf_len) : buf_(const_cast<char *>(buf)), buf_len_(buf_len) {}
  ~LogWriteBuf() {}

  const char *get_buf() const { return buf_; }
  int64_t get_buf_len() const { return buf_len_; }

  int assign(const char *buf, int64_t buf_len) {
    buf_ = const_cast<char *>(buf);
    buf_len_ = buf_len;
    return 0;
  }

private:
  char *buf_;
  int64_t buf_len_;
};

}  // namespace palf
}  // namespace oceanbase
#endif
