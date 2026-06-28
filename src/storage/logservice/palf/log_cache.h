/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/log_cache.h */

#ifndef OCEANBASE_LOGSERVICE_LOG_CACHE_
#define OCEANBASE_LOGSERVICE_LOG_CACHE_
#include <cstring>
#include <cstdlib>
#include "log_define.h"
#include "lsn.h"

namespace oceanbase {
namespace palf {

/**
 * LogCache — in-memory log read cache.
 * Simplified from OB 4.4.2 log_cache.h.
 */
class LogCache {
public:
  LogCache() : buf_(nullptr), buf_len_(0), begin_lsn_(), end_lsn_(), is_inited_(false) {}
  ~LogCache() { destroy(); }

  int init(int64_t capacity = 32 * 1024 * 1024) {
    buf_ = static_cast<char *>(std::malloc(capacity));
    if (nullptr == buf_) return -1;
    buf_len_ = capacity;
    is_inited_ = true;
    return 0;
  }

  void destroy() {
    if (buf_ != nullptr) { std::free(buf_); buf_ = nullptr; }
    buf_len_ = 0; is_inited_ = false;
  }

  int put(const LSN &lsn, const char *data, int64_t data_len) {
    // Simple implementation: store single entry (not a real cache)
    if (data_len > buf_len_) return -1;
    std::memcpy(buf_, data, data_len);
    begin_lsn_ = lsn;
    end_lsn_ = LSN(lsn.val_ + data_len);
    return 0;
  }

  int get(const LSN &lsn, int64_t read_size, char *buf, int64_t &out_size) {
    if (!is_inited_) return -1;
    if (lsn.val_ >= begin_lsn_.val_ && lsn.val_ < end_lsn_.val_) {
      int64_t offset = lsn.val_ - begin_lsn_.val_;
      int64_t remain = end_lsn_.val_ - lsn.val_;
      out_size = (read_size < remain) ? read_size : remain;
      std::memcpy(buf, buf_ + offset, out_size);
      return 0;
    }
    return -1; // Not cached
  }

  char *get_buf() { return buf_; }
  int64_t get_buf_len() const { return buf_len_; }

private:
  char *buf_;
  int64_t buf_len_;
  LSN begin_lsn_;
  LSN end_lsn_;
  bool is_inited_;
};

}  // namespace palf
}  // namespace oceanbase
#endif
