/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
Refer to: /opt/oceanbase4.4.2/src/logservice/palf/log_io_adapter.h */

#include "log_io_adapter.h"
#include <fcntl.h>
#include <unistd.h>
#include <sstream>

namespace oceanbase {
namespace palf {

LogIOAdapter::LogIOAdapter() : is_inited_(false) {}
LogIOAdapter::~LogIOAdapter() { destroy(); }

int LogIOAdapter::init(const char *log_dir)
{
  log_dir_ = log_dir;
  std::string cmd = "mkdir -p " + log_dir_;
  ::system(cmd.c_str());
  is_inited_ = true;
  return 0;
}

void LogIOAdapter::destroy() { is_inited_ = false; }

int LogIOAdapter::pwrite(int64_t file_id, const char *buf, int64_t buf_len, int64_t offset)
{
  std::ostringstream oss;
  oss << log_dir_ << "/" << file_id;
  int fd = ::open(oss.str().c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
  if (fd < 0) return -1;
  if (::lseek(fd, offset, SEEK_SET) < 0) { ::close(fd); return -1; }
  ssize_t written = ::write(fd, buf, buf_len);
  ::fsync(fd);
  ::close(fd);
  return (written == buf_len) ? 0 : -1;
}

int LogIOAdapter::pread(int64_t file_id, char *buf, int64_t buf_len,
                         int64_t offset, int64_t &out_size)
{
  out_size = 0;
  std::ostringstream oss;
  oss << log_dir_ << "/" << file_id;
  int fd = ::open(oss.str().c_str(), O_RDONLY);
  if (fd < 0) return -1;
  if (::lseek(fd, offset, SEEK_SET) < 0) { ::close(fd); return -1; }
  ssize_t n = ::read(fd, buf, buf_len);
  ::close(fd);
  if (n < 0) return -1;
  out_size = n;
  return 0;
}

}  // namespace palf
}  // namespace oceanbase
