/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/log_io_adapter.h */

#ifndef OCEANBASE_LOGSERVICE_LOG_IO_ADAPTER_
#define OCEANBASE_LOGSERVICE_LOG_IO_ADAPTER_
#include <cstdint>
#include <string>

namespace oceanbase {
namespace palf {

/**
 * LogIOAdapter — async disk I/O adapter for log operations.
 * Simplified from OB 4.4.2 log_io_adapter.h.
 *
 * For single-node mystorage, uses synchronous pwrite directly.
 */
class LogIOAdapter {
public:
  LogIOAdapter();
  ~LogIOAdapter();

  int init(const char *log_dir);
  void destroy();

  // Synchronous write (simplified)
  int pwrite(int64_t file_id, const char *buf, int64_t buf_len, int64_t offset);
  int pread(int64_t file_id, char *buf, int64_t buf_len, int64_t offset,
            int64_t &out_size);

  const std::string &get_log_dir() const { return log_dir_; }

private:
  std::string log_dir_;
  bool is_inited_;
};

}  // namespace palf
}  // namespace oceanbase
#endif
